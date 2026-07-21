// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_lod.h
/// @brief G3 distance-LOD selection for the GPU grass field — pure, GL-free, unit-tested.
///        Design: docs/phases/phase_10_meadow_gpu_grass_design.md §5.3.
///
/// Two LOD axes (design §5.3):
///   * **segment tier** (per chunk, coarse): a chunk's whole draw uses one blade vertex
///     count (2N+1) — stepped near→mid→far, keyed on the chunk's *nearest* point so the
///     affected blades are near-sub-pixel at the step.
///   * **blade-count fade** (per blade, smooth): far chunks submit a smaller blade
///     fraction; the shrinking blades taper to zero *geometrically* over distance rather
///     than being hard-culled at a chunk boundary, so nothing pops.
///
/// `grassKeptFraction(d)` is the seam: the CPU evaluates it at the chunk's nearest point
/// to size `instanceCount`, and the vertex shader evaluates the *identical* formula per
/// blade (at each blade's own root-to-camera distance) to fade the drop set. Same curve on
/// both sides = no visible discontinuity. A prefix of the shuffled per-chunk seed order is
/// a spatially-uniform subset (§5.2), so drawing the first `keptFraction·count` blades thins
/// the field evenly rather than dropping a spatial slab.

#pragma once

#include <algorithm>

namespace Vestige
{

/// @brief Distance thresholds + per-tier segment/fraction levels for the grass LOD.
///        Distances are metres from the chunk's nearest point to the camera. Defaults are
///        tuned for the 256 m meadow with 16 m chunks (band width ≥ the ~22.6 m chunk
///        diagonal so a chunk's near-edge blades finish fading before its far-edge ones,
///        §5.3 precondition).
struct GrassLodBands
{
    float nearMid = 45.0f;    ///< near→mid tier boundary (m).
    float midFar = 95.0f;     ///< mid→far tier boundary (m).
    float cullDist = 170.0f;  ///< beyond this the chunk is not drawn at all.
    float bandWidth = 24.0f;  ///< fade-band width leading up to each boundary (≥ chunk diagonal).

    int nearSegments = 7;     ///< N near — 2N+1 = 15 strip verts.
    int midSegments = 5;      ///< N mid — 11 verts.
    int farSegments = 3;      ///< N far — 7 verts.

    float nearFraction = 1.0f;   ///< blades kept close in.
    float midFraction = 0.5f;    ///< blades kept in the mid band.
    float farFraction = 0.25f;   ///< blades kept far out.

    float rankBand = 0.12f;   ///< rank-space softness of the per-blade cutoff (shader only).
};

/// @brief Hermite smoothstep, matching GLSL `smoothstep` (C¹ at both edges). Used so the
///        CPU `keptFraction` curve is bit-for-bit the same shape as the shader's.
inline float grassSmooth01(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1.0e-4f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// @brief Fraction of a chunk's blades kept at camera distance `d` (§5.3). Monotonic
///        non-increasing: `nearFraction` close in, smoothstepping down to `midFraction`
///        across [nearMid-bandWidth, nearMid], then to `farFraction` across
///        [midFar-bandWidth, midFar]. The GLSL twin in grass.vert.glsl MUST match.
inline float grassKeptFraction(float d, const GrassLodBands& b)
{
    // mix(a, c, t) = a + (c - a) * t  (GLSL mix; std::lerp is C++20, project is C++17).
    const float tMid = grassSmooth01(b.nearMid - b.bandWidth, b.nearMid, d);
    float f = b.nearFraction + (b.midFraction - b.nearFraction) * tMid;
    const float tFar = grassSmooth01(b.midFar - b.bandWidth, b.midFar, d);
    f = f + (b.farFraction - f) * tFar;
    return f;
}

/// @brief The per-chunk LOD decision from the chunk's nearest-point distance.
struct GrassLod
{
    bool draw = true;              ///< false → chunk distance-culled (skip its draw).
    int segments = 7;              ///< N for the whole chunk draw (2N+1 strip verts).
    float instanceFraction = 1.0f; ///< instanceCount = ceil(this · chunkCount).
};

/// @brief Pick the segment tier + submitted blade fraction for a chunk at `nearestDist`
///        (§5.3). Segment tier steps hard at the boundaries (keyed on nearest → sub-pixel);
///        `instanceFraction` is the continuous `grassKeptFraction` so the CPU instanceCount
///        and the shader's per-blade fade never disagree about which blades exist.
inline GrassLod grassLodForDistance(float nearestDist, const GrassLodBands& b)
{
    GrassLod lod;
    if (nearestDist >= b.cullDist)
    {
        lod.draw = false;
        lod.instanceFraction = 0.0f;
        lod.segments = b.farSegments;
        return lod;
    }
    lod.instanceFraction = grassKeptFraction(nearestDist, b);
    lod.segments = (nearestDist < b.nearMid) ? b.nearSegments
                 : (nearestDist < b.midFar)  ? b.midSegments
                                             : b.farSegments;
    return lod;
}

} // namespace Vestige
