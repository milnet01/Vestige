// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_grass_placement.cpp
/// @brief G2 — pure placement predicates + the deterministic blade seed builder
///        (design docs/phases/phase_10_meadow_gpu_grass_design.md §5.2/§8).
///
/// These pin the PCG gating contract without a GL context or a Terrain, the same way the
/// billboard `scatterProps` predicate tests do: spawn probability ∝ grass weight (0 grass
/// weight → 0 blades), slope rejection above the cutoff, pond-disc rejection, and a
/// reproducible seed → the same blade within its configured tall/wild ranges.

#include "environment/grass_placement.h"

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{
GrassConfig makeConfig()
{
    GrassConfig c;   // tall & wild defaults
    c.slopeCutoff = 0.55f;
    return c;
}
} // namespace

// 0 grass weight → never spawns, regardless of the roll (thins to bare earth over dirt).
TEST(GrassPlacement, ZeroGrassWeightNeverSpawns_G2)
{
    const GrassConfig c = makeConfig();
    for (float roll = 0.0f; roll < 1.0f; roll += 0.1f)
    {
        EXPECT_FALSE(grassCandidateAccepted(1.0f /*flat*/, 0.0f /*no grass*/, roll, c));
    }
}

// Spawn probability tracks the grass weight: a roll below the weight accepts, above rejects.
TEST(GrassPlacement, SpawnProbabilityTracksGrassWeight_G2)
{
    const GrassConfig c = makeConfig();
    EXPECT_TRUE(grassCandidateAccepted(1.0f, 0.8f, 0.5f, c));    // roll < weight
    EXPECT_FALSE(grassCandidateAccepted(1.0f, 0.3f, 0.5f, c));   // roll > weight
    EXPECT_TRUE(grassCandidateAccepted(1.0f, 1.0f, 0.99f, c));   // full grass, high roll
}

// A slope steeper than the cutoff is rejected even on full grass with a zero roll.
TEST(GrassPlacement, SteepSlopeRejected_G2)
{
    const GrassConfig c = makeConfig();
    EXPECT_FALSE(grassCandidateAccepted(0.40f /*< 0.55 cutoff*/, 1.0f, 0.0f, c));
    EXPECT_TRUE(grassCandidateAccepted(0.60f /*> cutoff*/, 1.0f, 0.0f, c));
}

// The pond exclusion disc rejects interior points and passes exterior ones; radius<=0 off.
TEST(GrassPlacement, ExclusionDisc_G2)
{
    const glm::vec2 center(10.0f, -4.0f);
    EXPECT_TRUE(grassInExclusionDisc(10.0f, -4.0f, center, 3.0f));   // dead centre
    EXPECT_TRUE(grassInExclusionDisc(12.0f, -4.0f, center, 3.0f));   // 2 m < 3 m radius
    EXPECT_FALSE(grassInExclusionDisc(15.0f, -4.0f, center, 3.0f));  // 5 m > radius
    EXPECT_FALSE(grassInExclusionDisc(10.0f, -4.0f, center, 0.0f));  // disabled
}

// The same scatter key reproduces the same blade, and every hash-derived factor lands in
// its configured tall/wild range.
TEST(GrassPlacement, BladeSeedDeterministicAndInRange_G2)
{
    const GrassConfig c = makeConfig();
    const glm::vec3 root(3.0f, 1.5f, -7.0f);

    const GrassBlade a = makeGrassBlade(root, 12345u, c);
    const GrassBlade b = makeGrassBlade(root, 12345u, c);
    EXPECT_EQ(a.height, b.height);
    EXPECT_EQ(a.facingAngle, b.facingAngle);
    EXPECT_EQ(a.lean, b.lean);
    EXPECT_EQ(a.width, b.width);
    EXPECT_EQ(a.hash, b.hash);
    EXPECT_EQ(a.rootPos, root);

    // Ranges across many keys.
    for (std::uint32_t k = 0; k < 500; ++k)
    {
        const GrassBlade s = makeGrassBlade(root, k * 2654435761u + 1u, c);
        EXPECT_GE(s.height, c.minHeight);
        EXPECT_LE(s.height, c.maxHeight);
        EXPECT_GE(s.width, c.minWidth);
        EXPECT_LE(s.width, c.maxWidth);
        EXPECT_GE(s.lean, c.minLean);
        EXPECT_LE(s.lean, c.maxLean);
        EXPECT_GE(s.facingAngle, 0.0f);
        EXPECT_LE(s.facingAngle, 6.2831854f);
    }
}
