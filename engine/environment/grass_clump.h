// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_clump.h
/// @brief GL-free "clumping" field — the "field, not tufts" layer for GPU grass
///        (design docs/phases/phase_10_meadow_gpu_grass_design.md §5.2a).
///
/// A pure function of world XZ: it scatters one jittered **clump centre** per cell of a
/// coarse grid, and over the 3×3 neighbourhood forms a smooth-kernel weighted average of
/// per-clump factors (height / lean direction / tint / bend) plus a nearest-only wind
/// phase. Blades read these and conform toward their clump, so the field reads as natural
/// **tussocks** instead of uniform blades. This header is the **CPU hand-mirror** of the
/// GLSL twin in grass.vert.glsl (Rule-7 parity, §8) and is also what the CPU placement /
/// chunk-AABB path evaluates (§5.2a: pad the AABB by clump-max height/lean).
///
/// Committed constants (design §5.2a, provably-correct envelope): jitter radius
/// `0.25·cellSize`, `kernelR = cellSize` → at least one of the 3×3 centres is within
/// `kernelR` everywhere (`Σwᵢ > 0`, so the `ε` fallback is an unreachable float-safety net)
/// **and** every out-of-3×3 centre is beyond `kernelR` (C⁰). The hash is integer-only so
/// GLSL `uint` and C++ `uint32_t` bit-match exactly.
#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

namespace Vestige
{

/// @brief Blended per-blade clump influence returned by `grassClump`.
struct GrassClump
{
    float height = 1.0f;                      ///< Height multiplier (~0.6–1.6×).
    glm::vec2 leanDir{1.0f, 0.0f};            ///< Unit XZ direction the clump leans toward.
    float tint = 0.0f;                        ///< Colour drift, [-1, 1] (caller scales).
    float bend = 0.0f;                        ///< Tussock flop, [0, 1].
    float phase = 0.0f;                       ///< Wind phase (nearest-only), [0, 2π).
};

/// @brief Clump-field tunables. `defaultClumpParams` builds the design's committed set.
struct GrassClumpParams
{
    float cellSize = 1.0f;                    ///< Coarse-grid cell size = clumpScale (m).
    float kernelR = 1.0f;                     ///< Blend kernel radius (design: = cellSize).
    float jitterRadius = 0.25f;               ///< Clump-centre jitter radius (≤ 0.25·cellSize).
    float epsilon = 1.0e-6f;                  ///< Σwᵢ floor → nearest-cell fallback (no NaN).
};

inline GrassClumpParams defaultClumpParams(float cellSize)
{
    return GrassClumpParams{cellSize, cellSize, 0.25f * cellSize, 1.0e-6f};
}

// --- deterministic integer hash (GLSL-uint reproducible; no fract(sin(...))) ---------

/// @brief 32-bit integer finalizer (xxhash/PCG-style: only `*`, `^`, `>>` on `uint32`).
inline std::uint32_t grassHashU32(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/// @brief Hash a cell coordinate pair with a per-attribute salt → a fresh 32-bit word.
inline std::uint32_t grassCellHash(std::int32_t cellX, std::int32_t cellZ, std::uint32_t salt)
{
    std::uint32_t h = static_cast<std::uint32_t>(cellX) * 0x9E3779B1u
                    ^ static_cast<std::uint32_t>(cellZ) * 0x85EBCA77u
                    ^ salt * 0xC2B2AE3Du;
    return grassHashU32(h);
}

/// @brief Map a 32-bit word to a float in [0, 1).
inline float grassU32ToUnit(std::uint32_t h)
{
    return static_cast<float>(h) * (1.0f / 4294967296.0f);   // / 2^32
}

/// @brief GLSL-identical smoothstep (edge0 < edge1).
inline float grassSmoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// --- the clump field -----------------------------------------------------------------

namespace detail
{
// Per-attribute hash salts (arbitrary distinct constants).
constexpr std::uint32_t SALT_JITTER = 0x1000001u;
constexpr std::uint32_t SALT_HEIGHT = 0x2000003u;
constexpr std::uint32_t SALT_LEAN   = 0x3000005u;
constexpr std::uint32_t SALT_TINT   = 0x4000007u;
constexpr std::uint32_t SALT_BEND   = 0x5000009u;
constexpr std::uint32_t SALT_PHASE  = 0x600000Bu;

struct ClumpCell
{
    glm::vec2 centre;   // world-space jittered centre
    float height;       // 0.6–1.6
    glm::vec2 leanDir;  // unit
    float tint;         // [-1, 1]
    float bend;         // [0, 1]
    float phase;        // [0, 2π)
    std::uint32_t id;   // deterministic cell id (tie-break)
};

// The full per-cell factor set for cell (cx, cz).
inline ClumpCell clumpCell(std::int32_t cx, std::int32_t cz, const GrassClumpParams& p)
{
    ClumpCell c;
    const float jx = grassU32ToUnit(grassCellHash(cx, cz, SALT_JITTER));
    const float jr = grassU32ToUnit(grassCellHash(cx, cz, SALT_JITTER + 1u));
    // Polar jitter so the offset magnitude is bounded by jitterRadius (a true radius).
    const float ang = jx * 6.2831853f;
    const float rad = jr * p.jitterRadius;
    const glm::vec2 cellCentre((static_cast<float>(cx) + 0.5f) * p.cellSize,
                               (static_cast<float>(cz) + 0.5f) * p.cellSize);
    c.centre = cellCentre + glm::vec2(std::cos(ang), std::sin(ang)) * rad;

    c.height = 0.6f + 1.0f * grassU32ToUnit(grassCellHash(cx, cz, SALT_HEIGHT));   // 0.6–1.6
    const float la = grassU32ToUnit(grassCellHash(cx, cz, SALT_LEAN)) * 6.2831853f;
    c.leanDir = glm::vec2(std::cos(la), std::sin(la));
    c.tint = grassU32ToUnit(grassCellHash(cx, cz, SALT_TINT)) * 2.0f - 1.0f;       // [-1,1]
    c.bend = grassU32ToUnit(grassCellHash(cx, cz, SALT_BEND));                     // [0,1]
    c.phase = grassU32ToUnit(grassCellHash(cx, cz, SALT_PHASE)) * 6.2831853f;      // [0,2π)
    c.id = grassCellHash(cx, cz, 0u);
    return c;
}
} // namespace detail

/// @brief Evaluate the clump field at world XZ (design §5.2a). Smooth-kernel weighted
///        average over the 3×3 neighbourhood; scalar factors are C⁰, lean is a
///        renormalised vector blend (antipodal-guarded with a deterministic tie-break),
///        phase is nearest-only. The `Σwᵢ<ε` nearest-cell fallback guarantees no NaN.
inline GrassClump grassClump(float worldX, float worldZ, const GrassClumpParams& p)
{
    const std::int32_t baseX = static_cast<std::int32_t>(std::floor(worldX / p.cellSize));
    const std::int32_t baseZ = static_cast<std::int32_t>(std::floor(worldZ / p.cellSize));
    const glm::vec2 sample(worldX, worldZ);

    float sumW = 0.0f, sumH = 0.0f, sumT = 0.0f, sumB = 0.0f;
    glm::vec2 sumLean(0.0f);

    float nearestD2 = 3.402823e38f;        // for phase + fallback
    float bestW = -1.0f;                   // dominant weight (antipodal tie-break)
    detail::ClumpCell nearest{};
    detail::ClumpCell dominant{};

    for (std::int32_t dz = -1; dz <= 1; ++dz)
    {
        for (std::int32_t dx = -1; dx <= 1; ++dx)
        {
            const detail::ClumpCell c = detail::clumpCell(baseX + dx, baseZ + dz, p);
            const glm::vec2 delta = sample - c.centre;
            const float d2 = glm::dot(delta, delta);
            const float d = std::sqrt(d2);
            const float w = 1.0f - grassSmoothstep(0.0f, p.kernelR, d);

            sumW += w;
            sumH += w * c.height;
            sumT += w * c.tint;
            sumB += w * c.bend;
            sumLean += w * c.leanDir;

            if (d2 < nearestD2) { nearestD2 = d2; nearest = c; }
            // Dominant = max weight; deterministic tie-break on lowest cell id.
            if (w > bestW || (w == bestW && c.id < dominant.id))
            {
                bestW = w;
                dominant = c;
            }
        }
    }

    GrassClump out;
    if (sumW < p.epsilon)
    {
        // Unreachable in the committed envelope — a float-safety net (no 0/0 NaN).
        out.height = nearest.height;
        out.leanDir = nearest.leanDir;
        out.tint = nearest.tint;
        out.bend = nearest.bend;
        out.phase = nearest.phase;
        return out;
    }

    const float invW = 1.0f / sumW;
    out.height = sumH * invW;
    out.tint = sumT * invW;
    out.bend = sumB * invW;
    out.phase = nearest.phase;             // nearest-only (cyclic — never averaged)

    const float leanLen2 = glm::dot(sumLean, sumLean);
    if (leanLen2 < 1.0e-12f)
    {
        // Antipodal: opposing lean dirs at ~equal weight → dominant-weight dir (defined,
        // not continuous), deterministic tie-break already applied.
        out.leanDir = dominant.leanDir;
    }
    else
    {
        out.leanDir = sumLean / std::sqrt(leanLen2);
    }
    return out;
}

} // namespace Vestige
