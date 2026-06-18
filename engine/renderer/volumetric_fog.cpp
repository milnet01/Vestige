// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_fog.cpp
/// @brief Implementation of the froxel-grid coordinate math (slice 11.6).

#include "renderer/volumetric_fog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vestige
{

namespace
{

/// True when the config can produce a well-defined exponential mapping.
bool isFinite(const FroxelGridConfig& cfg)
{
    return cfg.resZ > 0 && cfg.near > 0.0f && cfg.far > cfg.near;
}

// --- Value-noise FBM density field (slice 11.8). Integer-hash core so the
// CPU spec below and the GLSL `fogDensityNoise` in volumetric_inject.comp.glsl
// agree bit-for-bit on the hash (Jarzynski & Olano, JCGT 2020); the float
// interpolation matches within a few ULPs. Every op here is mirrored verbatim
// in the shader — keep the two in lockstep (Rule 7). NEVER sin-hashing.

/// 3D lattice cell → 32-bit hash. `uint32_t` wraparound matches GLSL `uint`.
std::uint32_t noiseHash3(int x, int y, int z)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u
                    + static_cast<std::uint32_t>(y) * 0xd8163841u
                    + static_cast<std::uint32_t>(z) * 0xcb1ab31fu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

/// Hash → [0, 1). Top 24 bits / 2^24: exactly representable in float, so the
/// conversion is bit-identical to the shader's `float(h >> 8u) / 16777216.0`.
float hashToUnit(std::uint32_t h)
{
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

float vlerp(float a, float b, float t)
{
    return a + t * (b - a);
}

/// Trilinear value noise with a smoothstep fade. Returns [0, 1].
float valueNoise3(float px, float py, float pz)
{
    const float fpx = std::floor(px);
    const float fpy = std::floor(py);
    const float fpz = std::floor(pz);
    const int ix = static_cast<int>(fpx);
    const int iy = static_cast<int>(fpy);
    const int iz = static_cast<int>(fpz);
    const float tx = px - fpx, ty = py - fpy, tz = pz - fpz;
    const float ux = tx * tx * (3.0f - 2.0f * tx);
    const float uy = ty * ty * (3.0f - 2.0f * ty);
    const float uz = tz * tz * (3.0f - 2.0f * tz);

    const float c000 = hashToUnit(noiseHash3(ix,     iy,     iz));
    const float c100 = hashToUnit(noiseHash3(ix + 1, iy,     iz));
    const float c010 = hashToUnit(noiseHash3(ix,     iy + 1, iz));
    const float c110 = hashToUnit(noiseHash3(ix + 1, iy + 1, iz));
    const float c001 = hashToUnit(noiseHash3(ix,     iy,     iz + 1));
    const float c101 = hashToUnit(noiseHash3(ix + 1, iy,     iz + 1));
    const float c011 = hashToUnit(noiseHash3(ix,     iy + 1, iz + 1));
    const float c111 = hashToUnit(noiseHash3(ix + 1, iy + 1, iz + 1));

    const float x00 = vlerp(c000, c100, ux);
    const float x10 = vlerp(c010, c110, ux);
    const float x01 = vlerp(c001, c101, ux);
    const float x11 = vlerp(c011, c111, ux);
    const float y0  = vlerp(x00, x10, uy);
    const float y1  = vlerp(x01, x11, uy);
    return vlerp(y0, y1, uz);
}

/// FBM (lacunarity 2, gain 0.5), amplitude-normalised to [0, 1].
float fbm3(float px, float py, float pz, int octaves)
{
    float sum = 0.0f, amp = 0.5f, total = 0.0f;
    for (int o = 0; o < octaves; ++o)
    {
        sum   += amp * valueNoise3(px, py, pz);
        total += amp;
        px = px * 2.0f + 19.19f;
        py = py * 2.0f + 47.77f;
        pz = pz * 2.0f + 74.13f;
        amp *= 0.5f;
    }
    return total > 0.0f ? sum / total : 0.0f;
}

// --- Mist / ground-fog volume falloff (slice 11.11). Mirrored verbatim in the
// GLSL `fogVolumeDensity` in volumetric_inject.comp.glsl (Rule 7). ---

/// Cubic smoothstep on [edge0, edge1]; degenerates to a hard step when the
/// edges coincide or invert, so `edgeSoftness == 0` and zero-extent axes stay
/// finite and CPU↔GLSL parity-stable.
float smooth01(float edge0, float edge1, float x)
{
    const float denom = edge1 - edge0;
    if (denom <= 0.0f)
    {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((x - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// Falloff: 1 at/under `inner`, smoothly to 0 at/over `outer`.
float coreFade(float x, float inner, float outer)
{
    return 1.0f - smooth01(inner, outer, x);
}

} // namespace

int froxelCount(const FroxelGridConfig& cfg)
{
    if (cfg.resX <= 0 || cfg.resY <= 0 || cfg.resZ <= 0)
    {
        return 0;
    }
    return cfg.resX * cfg.resY * cfg.resZ;
}

float froxelSliceToViewDepth(const FroxelGridConfig& cfg, int slice)
{
    if (!isFinite(cfg))
    {
        return cfg.near;
    }

    // Clamp the slice into the valid range so the returned depth never
    // escapes [near, far] (callers may pass out-of-range indices).
    const int clamped = std::clamp(slice, 0, cfg.resZ - 1);
    const float t = (static_cast<float>(clamped) + 0.5f)
                  / static_cast<float>(cfg.resZ);

    // viewDepth = near * (far / near) ^ t
    return cfg.near * std::pow(cfg.far / cfg.near, t);
}

float viewDepthToFroxelSlice(const FroxelGridConfig& cfg, float viewDepth)
{
    if (!isFinite(cfg))
    {
        return 0.0f;
    }

    const float z = std::clamp(viewDepth, cfg.near, cfg.far);

    // slice = N * log(z / near) / log(far / near) - 0.5
    const float t = std::log(z / cfg.near) / std::log(cfg.far / cfg.near);
    return static_cast<float>(cfg.resZ) * t - 0.5f;
}

float froxelSliceBoundaryViewDepth(const FroxelGridConfig& cfg, int boundary)
{
    if (!isFinite(cfg))
    {
        return cfg.near;
    }

    // Integer boundary (no +0.5 centre offset): boundary(b) = near * (far/near)^(b/N).
    const int clamped = std::clamp(boundary, 0, cfg.resZ);
    const float t = static_cast<float>(clamped) / static_cast<float>(cfg.resZ);
    return cfg.near * std::pow(cfg.far / cfg.near, t);
}

float henyeyGreensteinPhase(float cosTheta, float g)
{
    const float gg = g * g;
    const float denom = 1.0f + gg - 2.0f * g * cosTheta;
    constexpr float PI = 3.14159265358979323846f;
    return (1.0f - gg) / (4.0f * PI * std::pow(std::max(denom, 1e-4f), 1.5f));
}

glm::vec2 froxelToScreenUV(const FroxelGridConfig& cfg, int i, int j)
{
    const float u = cfg.resX > 0
                  ? (static_cast<float>(i) + 0.5f) / static_cast<float>(cfg.resX)
                  : 0.0f;
    const float v = cfg.resY > 0
                  ? (static_cast<float>(j) + 0.5f) / static_cast<float>(cfg.resY)
                  : 0.0f;
    return {u, v};
}

float fogDensityNoise(const glm::vec3& worldPos, const FogNoiseParams& params, float time)
{
    const int octaves = std::clamp(params.octaves, 1, 5);
    const float dx = worldPos.x * params.frequency + params.windVelocity.x * time;
    const float dy = worldPos.y * params.frequency + params.windVelocity.y * time;
    const float dz = worldPos.z * params.frequency + params.windVelocity.z * time;
    const float n  = fbm3(dx, dy, dz, octaves);               // [0, 1]
    const float m  = 1.0f + params.strength * (2.0f * n - 1.0f);
    return std::clamp(m, 0.0f, 2.0f);
}

float fogVolumeDensity(const FogVolume& v, const glm::vec3& worldPos, float time)
{
    const float soft = std::clamp(v.edgeSoftness, 0.0f, 1.0f);
    const glm::vec3 d = worldPos - v.center;

    float falloff;
    if (v.shape == FogVolumeShape::Sphere)
    {
        const float outer = v.halfExtents.x;
        const float inner = outer * (1.0f - soft);
        const float dist  = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        falloff = coreFade(dist, inner, outer);
    }
    else
    {
        const glm::vec3 inner = v.halfExtents * (1.0f - soft);
        falloff = coreFade(std::fabs(d.x), inner.x, v.halfExtents.x)
                * coreFade(std::fabs(d.y), inner.y, v.halfExtents.y)
                * coreFade(std::fabs(d.z), inner.z, v.halfExtents.z);
    }

    // `falloff > 0` skips the FBM for froxels outside the volume (the common
    // case) — `0 * turb == 0`, so the result is identical with or without the
    // term and CPU↔GLSL parity holds; it just avoids the cost where it can't
    // matter.
    if (v.animSpeed != 0.0f && falloff > 0.0f)
    {
        // Turbulence reuses the slice-11.8 value-noise FBM field. F_TURB and
        // the octave count are provisional look constants, inlined here and in
        // the GLSL twin so the parity-test extractor sees a self-contained
        // function. Purely aesthetic — no reference data to fit (Rule 6).
        // TODO 11.10 / Formula Workbench: expose per-scene via the Fog panel.
        constexpr float F_TURB = 0.15f;
        const float n = fbm3(worldPos.x * F_TURB,
                             worldPos.y * F_TURB + time * v.animSpeed,
                             worldPos.z * F_TURB, 3); // [0,1]
        falloff *= n;
    }

    return falloff;
}

} // namespace Vestige
