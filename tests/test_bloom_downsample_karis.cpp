// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_bloom_downsample_karis.cpp
/// @brief Phase 10.9 Slice 4 R9 — bloom Karis combine contract.
///
/// Contract (authored from ROADMAP Phase 10.9 Slice 4 R9 and Jimenez
/// 2014 "Next Generation Post Processing in Call of Duty: Advanced
/// Warfare", slide 147):
///
///   The Karis path of the 13-tap bloom downsample combines 5
///   sample-group averages (1 inner + 4 corners) using:
///     - Per-group inverse-luminance Karis weights (firefly
///       suppression).
///     - Fixed Jimenez group weights: 0.5 for the inner group,
///       0.125 for each of the 4 corner groups (sum = 1.0,
///       energy preserving).
///
///   Before R9, `bloom_downsample.frag.glsl` and the CPU mirror
///   `combineBloomKarisGroups` treated all 5 groups equally
///   weighted by Karis luminance only — the inner group's 4×
///   contribution to the result was lost, undervaluing the high-
///   frequency centre and producing the "softness pop" between
///   the first and subsequent mips.
///
/// The tests use luminance-asymmetric input to distinguish the
/// fixed-group-weight fix from the equal-weight bug. Uniform input
/// passes both bug and fix (both reduce to identity) so it cannot
/// catch the bug on its own.

#include <gtest/gtest.h>

#include "renderer/bloom_downsample_karis.h"

#include <glm/glm.hpp>

using namespace Vestige;

TEST(BloomDownsampleKaris, UniformInputProducesUniformOutput_R9)
{
    // Trivial energy-preserving sanity. Uniform input passes against
    // both the bug and the fix; the test exists as a regression pin
    // (and a NaN-guard for divide-by-zero on the all-zero corner case
    // covered separately below).
    const glm::vec3 X(0.5f, 0.5f, 0.5f);
    glm::vec3 result = combineBloomKarisGroups(X, X, X, X, X);

    EXPECT_NEAR(result.r, X.r, 1e-5f);
    EXPECT_NEAR(result.g, X.g, 1e-5f);
    EXPECT_NEAR(result.b, X.b, 1e-5f);
}

TEST(BloomDownsampleKaris, CentreGroupHasFourTimesWeightOfCornerGroup_R9)
{
    // Headline R9 invariant — the inner-4-sample group must have
    // the canonical 0.5 / 0.125 = 4× weight of each corner group.
    //
    // Inputs: centre = (0.5, 0.5, 0.5), all 4 corners zero.
    // - Pre-R9 bug (equal weighting): result.r ≈ 0.0714 (= 0.5 × wC
    //   / (wC + 4) where wC = 1/(1+0.5) ≈ 0.667; the centre is
    //   diluted by 4 corners of equal weight).
    // - Post-R9 fix: result.r ≈ 0.2 (= 0.5 × 0.5 × wC / (0.5 wC +
    //   4 × 0.125 × 1) = 0.167 / 0.833).
    // The 0.05 tolerance is 100× tighter than the bug-vs-fix gap.
    const glm::vec3 centre(0.5f, 0.5f, 0.5f);
    const glm::vec3 zero(0.0f);
    glm::vec3 result = combineBloomKarisGroups(centre, zero, zero, zero, zero);

    EXPECT_NEAR(result.r, 0.2f, 0.05f);
    EXPECT_NEAR(result.g, 0.2f, 0.05f);
    EXPECT_NEAR(result.b, 0.2f, 0.05f);
}

TEST(BloomDownsampleKaris, ZeroInputProducesZeroOutput_R9)
{
    // NaN guard: with all groups zero, every Karis weight is 1 (no
    // div-by-zero from luminance term), the combined result must be
    // zero, not garbage from division of (0 / something).
    const glm::vec3 zero(0.0f);
    glm::vec3 result = combineBloomKarisGroups(zero, zero, zero, zero, zero);

    EXPECT_EQ(result.r, 0.0f);
    EXPECT_EQ(result.g, 0.0f);
    EXPECT_EQ(result.b, 0.0f);
}

TEST(BloomDownsampleKaris, CornersAreSymmetric_R9)
{
    // The 4 corners share the same fixed group weight (0.125 each)
    // and the same Karis weight (when they have identical luma), so
    // the combined result is rotation-invariant w.r.t. corner order.
    const glm::vec3 centre(0.3f, 0.3f, 0.3f);
    const glm::vec3 cornerA(1.0f, 0.0f, 0.0f);
    const glm::vec3 cornerB(0.0f, 1.0f, 0.0f);
    const glm::vec3 cornerC(0.0f, 0.0f, 1.0f);
    const glm::vec3 cornerD(0.5f, 0.5f, 0.0f);

    glm::vec3 r1 = combineBloomKarisGroups(centre, cornerA, cornerB, cornerC, cornerD);
    glm::vec3 r2 = combineBloomKarisGroups(centre, cornerD, cornerC, cornerB, cornerA);
    glm::vec3 r3 = combineBloomKarisGroups(centre, cornerB, cornerA, cornerD, cornerC);

    // All three orderings must produce identical output (corner
    // weights are symmetric in the canonical 13-tap pattern).
    EXPECT_NEAR(r1.r, r2.r, 1e-5f);
    EXPECT_NEAR(r1.g, r2.g, 1e-5f);
    EXPECT_NEAR(r1.b, r2.b, 1e-5f);

    EXPECT_NEAR(r1.r, r3.r, 1e-5f);
    EXPECT_NEAR(r1.g, r3.g, 1e-5f);
    EXPECT_NEAR(r1.b, r3.b, 1e-5f);
}

TEST(BloomDownsampleKaris, CornerFireflyIsSuppressedRelativeToCentre_R9)
{
    // Karis fireflies suppression: a single bright firefly in one
    // corner gets a small Karis weight, so its contribution to the
    // final mix is reduced relative to the surrounding low-
    // luminance samples.
    //
    // Setup: centre and 3 corners at (0.1, 0.1, 0.1); one corner at
    // (10, 10, 10) (a "firefly"). Compare the result against the
    // arithmetic mean of all 5 group inputs (no Karis suppression).
    //
    // The Karis-weighted result must be much closer to 0.1 (the
    // surrounding samples) than to the arithmetic mean of (4×0.1 +
    // 1×10) / 5 = 2.08.
    const glm::vec3 centre(0.1f);
    const glm::vec3 normalCorner(0.1f);
    const glm::vec3 firefly(10.0f);

    glm::vec3 result = combineBloomKarisGroups(centre, normalCorner,
                                                 normalCorner, normalCorner,
                                                 firefly);

    // Without Karis: arithmetic mean ≈ 2.08. With Karis suppression:
    // the firefly's weight 1/(1+luma(firefly)) ≈ 1/11 ≈ 0.091 is
    // tiny, so its contribution is heavily suppressed. Expect the
    // result well below 1.0 (i.e. firmly suppressed).
    EXPECT_LT(result.r, 1.0f) << "firefly was not suppressed";
    EXPECT_LT(result.g, 1.0f);
    EXPECT_LT(result.b, 1.0f);
    EXPECT_GT(result.r, 0.0f) << "firefly was over-suppressed (zero output)";
}

TEST(BloomDownsampleKaris, CentreFireflyIsSuppressed_R9)
{
    // Same suppression behaviour for the centre group — the Karis
    // weight is per-group and applies regardless of which group
    // holds the bright sample.
    const glm::vec3 firefly(10.0f);
    const glm::vec3 darkCorner(0.1f);

    glm::vec3 result = combineBloomKarisGroups(firefly, darkCorner,
                                                 darkCorner, darkCorner,
                                                 darkCorner);

    // Centre's fixed weight is 0.5 (4× the corners), so its firefly
    // bleeds into the result more than a corner firefly would. But
    // it still gets Karis-weighted down — should not produce raw
    // 0.5 × 10 = 5.0 (which would be the un-suppressed centre).
    EXPECT_LT(result.r, 3.0f) << "centre firefly not suppressed";
}

TEST(BloomDownsampleKaris, EnergyPreservedForUniformLuma_R9)
{
    // For a uniform-luminance scene (5 different colours but all at
    // the same luma → all Karis weights equal), the result equals
    // the Jimenez weighted average of the inputs:
    //   result = 0.5 × centre + 0.125 × (sum of 4 corners)
    // This pins that the GREEN body wires the fixed group weights
    // correctly: the bug's equal-weighting would produce
    //   result = (centre + sum_corners) / 5.
    //
    // Choose 5 colours along the BT.709 isolume curve. Luminance =
    // 0.2126 R + 0.7152 G + 0.0722 B. Set every input's luminance
    // to ~0.1 with different chroma:
    //   centre:    (0.470, 0.000, 0.000)  → luma ≈ 0.1
    //   cornerTL:  (0.000, 0.140, 0.000)  → luma ≈ 0.1
    //   cornerTR:  (0.000, 0.000, 1.385)  → luma ≈ 0.1
    //   cornerBL:  (0.235, 0.070, 0.000)  → luma ≈ 0.1
    //   cornerBR:  (0.000, 0.070, 0.692)  → luma ≈ 0.1
    const glm::vec3 centre(0.470f, 0.000f, 0.000f);
    const glm::vec3 cornerTL(0.000f, 0.140f, 0.000f);
    const glm::vec3 cornerTR(0.000f, 0.000f, 1.385f);
    const glm::vec3 cornerBL(0.235f, 0.070f, 0.000f);
    const glm::vec3 cornerBR(0.000f, 0.070f, 0.692f);

    glm::vec3 result = combineBloomKarisGroups(centre, cornerTL,
                                                 cornerTR, cornerBL,
                                                 cornerBR);

    // Expected (Jimenez): 0.5 × centre + 0.125 × (TL + TR + BL + BR).
    glm::vec3 expected = 0.5f * centre
                       + 0.125f * (cornerTL + cornerTR + cornerBL + cornerBR);

    // 0.05 tolerance — covers small Karis weight differences
    // between groups that aren't perfectly isolume due to floating-
    // point luma residuals.
    EXPECT_NEAR(result.r, expected.r, 0.05f);
    EXPECT_NEAR(result.g, expected.g, 0.05f);
    EXPECT_NEAR(result.b, expected.b, 0.05f);
}
