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

}  // namespace Vestige
