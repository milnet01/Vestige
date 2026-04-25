// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file bloom_downsample_karis.h
/// @brief CPU mirror of the Karis combine path in
/// `bloom_downsample.frag.glsl`. Pure-math so the energy-weighting
/// contract is testable without a GL context.
///
/// Reference: Jimenez 2014 "Next Generation Post Processing in Call
/// of Duty: Advanced Warfare", slide 147 (13-tap downsample with
/// 5 sample groups and 0.5 / 0.125×4 weighting). Karis fireflies
/// suppression: weight each group inversely by its luminance per
/// Karis 2013 SIGGRAPH talk on tone-mapping fireflies.
///
/// Before R9, the Karis path treated all 5 groups equally weighted
/// by `karisWeight(g_i) = 1 / (1 + luma(g_i))`, dropping the
/// canonical 0.5 / 0.125×4 group weights. Result: first-mip centre
/// undervalued → "softness pop" + energy loss. R9 restores the
/// fixed Jimenez group weights composed with the Karis weights.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// BT.709 luminance (Rec. 709 weights, used by both the GLSL shader
/// and this CPU mirror).
inline float bloomLuminance(const glm::vec3& c)
{
    return c.r * 0.2126f + c.g * 0.7152f + c.b * 0.0722f;
}

/// Karis weight: inverse of (1 + luminance). Suppresses bright
/// outliers (fireflies) in the per-group average.
inline float bloomKarisWeight(const glm::vec3& c)
{
    return 1.0f / (1.0f + bloomLuminance(c));
}

/// @brief Combine the 5 sample-group averages into a single
/// downsampled value using the canonical Jimenez group weights
/// (0.5 centre + 0.125 each corner) modulated by per-group Karis
/// luminance weights.
///
/// @param centre   Inner 4-sample group ((j+k+l+m)/4 in the GLSL).
/// @param cornerTL Top-left 4-sample group   ((a+b+d+e)/4).
/// @param cornerTR Top-right 4-sample group  ((b+c+e+f)/4).
/// @param cornerBL Bottom-left 4-sample group ((d+e+g+h)/4).
/// @param cornerBR Bottom-right 4-sample group ((e+f+h+i)/4).
///
/// Energy preservation: for uniform input (all groups identical,
/// all Karis weights equal), the combined result equals the input.
/// The fixed group weights `0.5 + 4 × 0.125 = 1.0` sum to unity.
inline glm::vec3 combineBloomKarisGroups(const glm::vec3& centre,
                                           const glm::vec3& cornerTL,
                                           const glm::vec3& cornerTR,
                                           const glm::vec3& cornerBL,
                                           const glm::vec3& cornerBR)
{
    // RED stub — mirrors the pre-R9 GLSL bug: all 5 groups treated
    // equally weighted by Karis luminance only, dropping the
    // Jimenez 0.5 (centre) + 0.125×4 (corners) group weights.
    // Result: first-mip centre is undervalued, "softness pop" and
    // energy loss for luminance-asymmetric input. The green commit
    // restores the fixed group weights composed with Karis.
    const float wC  = bloomKarisWeight(centre);
    const float wTL = bloomKarisWeight(cornerTL);
    const float wTR = bloomKarisWeight(cornerTR);
    const float wBL = bloomKarisWeight(cornerBL);
    const float wBR = bloomKarisWeight(cornerBR);

    const glm::vec3 numerator =
        centre * wC
      + cornerTL * wTL
      + cornerTR * wTR
      + cornerBL * wBL
      + cornerBR * wBR;

    const float denominator = wC + wTL + wTR + wBL + wBR;
    return numerator / denominator;
}

} // namespace Vestige
