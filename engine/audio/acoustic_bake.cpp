// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file acoustic_bake.cpp
/// @brief AX3 B2 — offline image-source impulse-response bake core.

#include "audio/acoustic_bake.h"

#include "core/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>

namespace Vestige
{

namespace
{

/// Per-`SurfaceMaterial` mid-frequency absorption α (design §6.2 table). Keyed
/// by name via the switch below, so a reorder of the enum can't silently
/// misalign the values.
constexpr float absorptionFor(SurfaceMaterial m)
{
    switch (m)
    {
    case SurfaceMaterial::Default: return 0.04f;  // untagged → reflective bucket
    case SurfaceMaterial::Stone:   return 0.03f;
    case SurfaceMaterial::Wood:    return 0.10f;
    case SurfaceMaterial::Metal:   return 0.05f;
    case SurfaceMaterial::Cloth:   return 0.55f;
    case SurfaceMaterial::Sand:    return 0.30f;
    case SurfaceMaterial::Water:   return 0.02f;
    case SurfaceMaterial::Grass:   return 0.30f;
    case SurfaceMaterial::Dirt:    return 0.15f;
    case SurfaceMaterial::Glass:   return 0.03f;
    }
    return 0.04f;
}

/// Reflect a point across a plane (n·x + d = 0, |n| = 1).
inline glm::vec3 reflectAcrossPlane(const glm::vec3& p, const glm::vec4& plane)
{
    const glm::vec3 n(plane);
    const float signedDist = glm::dot(n, p) + plane.w;
    return p - 2.0f * signedDist * n;
}

/// A generated image source: its mirrored position, the running product of
/// reflection factors `Π√(1−αᵢ)`, and the facet it was last reflected across
/// (so the recursion never re-reflects across the same plane back toward the
/// source — the standard valid-image prune for a planar convex room).
struct ImageSource
{
    glm::vec3 pos;
    float reflectionProduct;
    int lastFacet;
    int order;
};

/// Deterministic 32-bit seed from a probe position + a base — so a re-bake of
/// an unchanged scene reproduces the same diffuse tail (B3 determinism).
std::uint32_t seedFromPosition(const glm::vec3& p, std::uint32_t base)
{
    std::uint32_t h = base;
    for (int i = 0; i < 3; ++i)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &p[i], sizeof(bits));
        h ^= bits + 0x9E3779B9u + (h << 6) + (h >> 2);
    }
    return h;
}

} // namespace

float surfaceMaterialAbsorption(SurfaceMaterial material)
{
    return absorptionFor(material);
}

float sabineRt60(float roomVolumeM3, const std::vector<ReflectingFacet>& facets)
{
    double absorptionArea = 0.0;  // Σ Sᵢ·αᵢ
    for (const ReflectingFacet& f : facets)
    {
        absorptionArea += static_cast<double>(f.area) * static_cast<double>(absorptionFor(f.material));
    }
    if (roomVolumeM3 <= 1e-6f || absorptionArea <= 1e-6)
    {
        return 0.0f;
    }
    return static_cast<float>(0.161 * static_cast<double>(roomVolumeM3) / absorptionArea);
}

float estimateRt60(const std::vector<float>& ir, int sampleRate)
{
    if (ir.empty() || sampleRate <= 0)
    {
        return 0.0f;
    }

    // Schroeder backward integration: EDC(i) = Σ_{j≥i} ir[j]².
    const size_t n = ir.size();
    std::vector<double> edc(n, 0.0);
    double running = 0.0;
    for (size_t i = n; i-- > 0;)
    {
        running += static_cast<double>(ir[i]) * static_cast<double>(ir[i]);
        edc[i] = running;
    }
    const double e0 = edc[0];
    if (e0 <= 0.0)
    {
        return 0.0f;
    }

    // Least-squares fit of level (dB) vs time over the T30 window (−5…−35 dB).
    const double invSr = 1.0 / static_cast<double>(sampleRate);
    double sumT = 0.0, sumL = 0.0, sumTT = 0.0, sumTL = 0.0;
    long count = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (edc[i] <= 0.0)
        {
            break;
        }
        const double levelDb = 10.0 * std::log10(edc[i] / e0);
        if (levelDb > -5.0)
        {
            continue;
        }
        if (levelDb < -35.0)
        {
            break;
        }
        const double t = static_cast<double>(i) * invSr;
        sumT += t;
        sumL += levelDb;
        sumTT += t * t;
        sumTL += t * levelDb;
        ++count;
    }
    if (count < 2)
    {
        return 0.0f;  // IR too short/silent to reach the T30 window
    }
    const double denom = static_cast<double>(count) * sumTT - sumT * sumT;
    if (std::abs(denom) < 1e-12)
    {
        return 0.0f;
    }
    const double slope = (static_cast<double>(count) * sumTL - sumT * sumL) / denom;  // dB/s
    if (slope >= 0.0)
    {
        return 0.0f;  // non-decaying — not a valid reverb tail
    }
    return static_cast<float>(-60.0 / slope);
}

std::vector<float> bakeProbeIr(const std::vector<ReflectingFacet>& facets,
                               const glm::vec3& probePos,
                               float roomVolumeM3,
                               const BakeParams& params)
{
    const int order = std::clamp(params.reflectionOrder, 0, 8);
    const int sampleRate = std::max(params.sampleRate, 1);
    const float c = params.speedOfSound > 1.0f ? params.speedOfSound : 343.0f;

    // --- Early reflections: breadth-first image-source generation to order K.
    std::vector<ImageSource> images;
    images.push_back({probePos, 1.0f, -1, 0});  // order 0 = the source itself (not emitted)
    size_t head = 0;
    while (head < images.size())
    {
        const ImageSource cur = images[head++];
        if (cur.order >= order)
        {
            continue;
        }
        for (int fi = 0; fi < static_cast<int>(facets.size()); ++fi)
        {
            if (fi == cur.lastFacet)
            {
                continue;  // no immediate re-reflection across the same plane
            }
            const ReflectingFacet& f = facets[static_cast<size_t>(fi)];
            const glm::vec3 mirrored = reflectAcrossPlane(cur.pos, f.plane);
            const float factor = std::sqrt(std::max(0.0f, 1.0f - absorptionFor(f.material)));
            images.push_back({mirrored, cur.reflectionProduct * factor, fi, cur.order + 1});
        }
    }

    // Delay/amplitude of each image (order ≥ 1) as heard at the probe.
    struct Tap { int sample; float amp; };
    std::vector<Tap> taps;
    taps.reserve(images.size());
    int lastEarlySample = 0;
    const float maxSamples = params.maxIrSeconds * static_cast<float>(sampleRate);
    for (const ImageSource& img : images)
    {
        if (img.order == 0)
        {
            continue;  // direct path lives in the dry signal, not the reverb send
        }
        const float distance = glm::length(img.pos - probePos);
        if (distance < 1e-3f)
        {
            continue;  // degenerate coincident image
        }
        const float delay = distance / c;
        const int sample = static_cast<int>(std::lround(static_cast<double>(delay) * static_cast<double>(sampleRate)));
        if (static_cast<float>(sample) >= maxSamples)
        {
            continue;  // beyond the IR window
        }
        taps.push_back({sample, img.reflectionProduct / distance});
        lastEarlySample = std::max(lastEarlySample, sample);
    }

    // --- Late tail length from the room's Sabine RT60.
    const float rt60 = sabineRt60(roomVolumeM3, facets);
    const bool hasTail = rt60 > 1e-3f;
    int totalSamples = lastEarlySample + 1;
    if (hasTail)
    {
        const float irSeconds = std::clamp(rt60, params.minIrSeconds, params.maxIrSeconds);
        totalSamples = std::max(totalSamples,
                                static_cast<int>(std::ceil(irSeconds * static_cast<float>(sampleRate))));
    }
    if (totalSamples <= 0)
    {
        return {};
    }

    std::vector<float> ir(static_cast<size_t>(totalSamples), 0.0f);

    // Statistical diffuse tail: white noise under a −60 dB / RT60 envelope.
    // Gain kept below the early-reflection level so the distinct early taps stay
    // audible above the diffuse wash; the tail still dominates the decay energy.
    if (hasTail)
    {
        constexpr float kTailGain = 0.05f;
        const double decay = -3.0 / static_cast<double>(rt60);  // log10 amplitude slope (−60 dB over RT60)
        std::mt19937 rng(seedFromPosition(probePos, params.tailSeed));
        const double invMax = 1.0 / static_cast<double>(std::mt19937::max());
        for (int i = 0; i < totalSamples; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
            const float env = static_cast<float>(std::pow(10.0, decay * t));
            const float noise = static_cast<float>(static_cast<double>(rng()) * invMax * 2.0 - 1.0);
            ir[static_cast<size_t>(i)] = kTailGain * env * noise;
        }
    }

    // Sum the early reflections on top of the tail.
    for (const Tap& tap : taps)
    {
        if (tap.sample >= 0 && tap.sample < totalSamples)
        {
            ir[static_cast<size_t>(tap.sample)] += tap.amp;
        }
    }

    // Guard the [-1,1] PCM range. Not a normalisation — physical amplitudes are
    // meaningful — but overlapping taps could in principle exceed unity; clamp
    // and warn rather than silently emit an out-of-range sample (project Rule 5).
    int clamped = 0;
    for (float& s : ir)
    {
        if (s > 1.0f) { s = 1.0f; ++clamped; }
        else if (s < -1.0f) { s = -1.0f; ++clamped; }
    }
    if (clamped > 0)
    {
        Logger::warning("[AcousticBake] clamped " + std::to_string(clamped) +
                        " IR sample(s) to [-1,1] — overlapping reflections exceeded unity");
    }

    return ir;
}

} // namespace Vestige
