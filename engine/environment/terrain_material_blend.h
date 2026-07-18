// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_material_blend.h
/// @brief CPU spec for the terrain height-blend math (Phase 10 / slice A1),
///        pinned against the GLSL that lands in slice A2.
///
/// The terrain fragment shader blends the four ground layers (grass/rock/dirt/sand)
/// by splat weight using Mishkinis's depth-aware *height blend* rather than a linear
/// weighted average, so e.g. sand fills the cracks between cobbles instead of evenly
/// coating them (design §2 item 2, §4.3 step 3). That per-pixel blend is mirrored here
/// as a pure, GL-free function so a parity test can pin the math (project Rule 7: dual
/// CPU-spec / GPU-runtime impl with a parity test). When the GLSL `heightBlendWeights`
/// is authored in A2 it declares the same `DEPTH` literal and the identical formula.
///
/// Canonical formula (N-layer generalisation of Mishkinis, design §2 item 2):
///   hw_i = height_i + weight_i
///   ma   = max_i(hw_i) - depth          // `depth` = blend band width (~0.2)
///   b_i  = max(hw_i - ma, 0)
///   w_i  = b_i / Σ b_i
///
/// Divisor safety: the peak layer has `hw = max_i(hw_i)`, so `b_peak = depth`; hence
/// `Σ b_i ≥ depth`. Any `depth > 0` therefore guarantees a positive divisor with no
/// runtime epsilon. `depth > 0` is the function's contract (a debug assert guards it);
/// `depth = 0` is out of range (there every `b_i = 0` → 0/0).
#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace Vestige
{

/// Height-blend band width — the single source of truth shared by the CPU spec and the
/// GLSL (A2 declares the same literal). Wider band → softer cross-fade between layers.
/// Art-directed look constant (no reference dataset to fit against).
/// TODO: revisit via Formula Workbench if a look-target dataset is later captured
/// (design §4.4 item 2 — Rule 6 hand-code carve-out).
constexpr float TERRAIN_HEIGHT_BLEND_DEPTH = 0.2f;

/// Smallest depth the blend is valid at (the parity test's divisor-safety floor).
/// Below this the blend band collapses to a hard cut; `depth = 0` is 0/0.
constexpr float TERRAIN_HEIGHT_BLEND_DEPTH_MIN = 0.05f;

/// @brief Depth-aware height-blend weights for the four terrain layers.
///
/// Given each layer's packed height (material map B channel) and its splat weight,
/// returns per-layer blend weights that sum to 1 and are non-negative. A layer with a
/// tall height value "peeks through" a competitor of similar splat weight; a single
/// dominant layer resolves to weight 1.
///
/// @param height Per-layer height in [0,1] (material map B channel).
/// @param weight Per-layer splat weight in [0,1] (splatmap RGBA).
/// @param depth  Blend band width; **must be > 0** (contract; see file header).
/// @return Normalised blend weights (Σ = 1, each ≥ 0).
inline std::array<float, 4> heightBlendWeights(const std::array<float, 4>& height,
                                               const std::array<float, 4>& weight,
                                               float depth)
{
    assert(depth > 0.0f && "heightBlendWeights: depth must be > 0 (see terrain_material_blend.h)");

    std::array<float, 4> hw{};
    float maxHw = height[0] + weight[0];
    for (std::size_t i = 0; i < 4; ++i)
    {
        hw[i] = height[i] + weight[i];
        if (hw[i] > maxHw)
        {
            maxHw = hw[i];
        }
    }

    const float ma = maxHw - depth;

    std::array<float, 4> b{};
    float sum = 0.0f;
    for (std::size_t i = 0; i < 4; ++i)
    {
        b[i] = hw[i] - ma;
        if (b[i] < 0.0f)
        {
            b[i] = 0.0f;
        }
        sum += b[i];
    }
    // Σ b_i ≥ b_peak = depth > 0 (contract) → divisor is safe without an epsilon.

    std::array<float, 4> w{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        w[i] = b[i] / sum;
    }
    return w;
}

/// @brief Apply a tangent-space detail normal onto a world macro normal — CPU
///        mirror of the GLSL `whiteoutBlend` in `terrain.frag.glsl` (slice A3).
///
/// The top-down terrain projection's tangent frame is world-XZ (T = +X, B = +Z),
/// so the macro normal is reoriented from world (+Y up) into the detail's tangent
/// basis (+Z up), Ben-Golus Whiteout-blended, and reoriented back. This is the
/// pure, GL-free reference the A3 directional parity test pins the shader against
/// (project Rule 7). Both `macroN` and `detailN` are `{x, y, z}`; the result is a
/// unit-length world-space normal. A detail normal tilted +X raises the blended
/// normal's X (and −X lowers it) — the direction the test asserts.
inline std::array<float, 3> whiteoutBlendNormal(const std::array<float, 3>& macroN,
                                                const std::array<float, 3>& detailN)
{
    // world +Y up -> tangent +Z up: n1 = (macroN.x, macroN.z, macroN.y)
    const float n1x = macroN[0];
    const float n1y = macroN[2];
    const float n1z = macroN[1];

    // r = normalize( (n1.xy + detailN.xy, n1.z * detailN.z) )
    float rx = n1x + detailN[0];
    float ry = n1y + detailN[1];
    float rz = n1z * detailN[2];
    const float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
    rx /= rlen;
    ry /= rlen;
    rz /= rlen;

    // tangent +Z up -> world +Y up: (r.x, r.z, r.y)
    float wx = rx;
    float wy = rz;
    float wz = ry;
    const float wlen = std::sqrt(wx * wx + wy * wy + wz * wz);
    return {wx / wlen, wy / wlen, wz / wlen};
}

/// @brief Distance-tiling blend factor — CPU mirror of the GLSL
///        `smoothstep(u_distanceTiling.x, u_distanceTiling.y, dist)` (slice A4).
///
/// The textured ground fades from its near per-layer tiling to a coarser
/// far-scaled albedo over view distance to break the tile repeat across the far
/// field (design §4.3 step 1). This is the pure reference the A4 unit test pins:
/// 0 at/below `nearDist`, 1 at/above `farDist`, and Hermite-smooth (0.5 at the
/// midpoint) in between. `farDist` must be > `nearDist` (a valid smoothstep range).
inline float distanceTilingBlend(float dist, float nearDist, float farDist)
{
    assert(farDist > nearDist && "distanceTilingBlend: farDist must exceed nearDist");
    float t = (dist - nearDist) / (farDist - nearDist);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

/// @brief Grass→dirt patch weight for the meadow's earthy-bare-ground term
///        (3D_E-0038 C1). Given a 0..1 patch-noise value, the threshold above
///        which dirt begins, and the max amount, returns the fraction of grass to
///        convert to dirt: 0 below `threshold`, Hermite-smooth up to `amount` at
///        noise 1. `amount <= 0` disables it (returns 0), so the default-0 config
///        leaves every existing scene byte-unchanged. Pure + GL-free (unit-tested).
///        The caller applies it as `dirt += grass*w; grass *= (1 - w)` — a mass
///        transfer that conserves grass+dirt before the splat renormalize.
inline float grassDirtPatchWeight(float noise01, float threshold, float amount)
{
    if (amount <= 0.0f)
    {
        return 0.0f;
    }
    // Ramp over a fixed BAND above the threshold (not up to 1.0): the terrain's
    // fbm noise is a 3-octave average that clusters near its midrange and rarely
    // reaches 1.0, so a smoothstep(threshold, 1.0) would barely trigger. A 0.2-wide
    // band above the threshold gives clearly-read patches. Still: 0 below
    // threshold, Hermite-smooth up to `amount` (reached by `threshold + BAND`,
    // and at noise 1). BAND is fixed so there is no div-by-zero at threshold→1.
    constexpr float BAND = 0.2f;
    float t = (noise01 - threshold) / BAND;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float s = t * t * (3.0f - 2.0f * t);  // smoothstep
    return s * amount;
}

}  // namespace Vestige
