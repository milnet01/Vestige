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

// ---------------------------------------------------------------------------
// Shore reed band (3D_E-0702)
// ---------------------------------------------------------------------------

namespace
{
/// A pond at the origin: water at y = 2, band reaching 20 m, reeds ending 0.5 m up.
GrassConfig makeShoreConfig()
{
    GrassConfig c = makeConfig();
    c.exclusionCenter = glm::vec2(0.0f, 0.0f);
    c.shoreRadius = 20.0f;
    c.shoreWaterY = 2.0f;
    c.shoreRise = 0.5f;
    return c;
}
} // namespace

// The band is off by default: nothing is submerged and nothing is a reed.
TEST(GrassPlacement, ShoreBandOffByDefault)
{
    const GrassConfig c = makeConfig();
    EXPECT_FALSE(grassSubmerged(0.0f, 0.0f, -100.0f, c));
    EXPECT_EQ(grassShoreFactor(0.0f, 0.0f, 0.0f, c), 0.0f);
}

// Ground under the water surface grows nothing, but only inside the band's reach:
// a hollow elsewhere on the map that happens to sit lower than the pond keeps its grass.
TEST(GrassPlacement, SubmergedGroundRejectedOnlyNearThePond)
{
    const GrassConfig c = makeShoreConfig();
    EXPECT_TRUE(grassSubmerged(5.0f, 0.0f, 1.5f, c));     // pond bed
    EXPECT_FALSE(grassSubmerged(5.0f, 0.0f, 2.1f, c));    // bank just above the water
    EXPECT_FALSE(grassSubmerged(50.0f, 0.0f, 1.5f, c));   // low ground far from the pond
}

// Reeds are strongest at the waterline and gone `shoreRise` above it.
TEST(GrassPlacement, ShoreFactorFadesWithHeightAboveWater)
{
    const GrassConfig c = makeShoreConfig();
    const float atWater = grassShoreFactor(5.0f, 0.0f, 2.0f, c);
    const float halfway = grassShoreFactor(5.0f, 0.0f, 2.25f, c);
    const float atRise  = grassShoreFactor(5.0f, 0.0f, 2.5f, c);
    EXPECT_FLOAT_EQ(atWater, 1.0f);
    EXPECT_GT(halfway, 0.0f);
    EXPECT_LT(halfway, 1.0f);
    EXPECT_FLOAT_EQ(atRise, 0.0f);
    EXPECT_FLOAT_EQ(grassShoreFactor(5.0f, 0.0f, 9.0f, c), 0.0f);   // high ground
}

// The band fades out before its reach ends, so there is no visible ring where it stops.
TEST(GrassPlacement, ShoreFactorFadesAtTheEdgeOfItsReach)
{
    const GrassConfig c = makeShoreConfig();
    EXPECT_FLOAT_EQ(grassShoreFactor(10.0f, 0.0f, 2.0f, c), 1.0f);   // well inside
    const float nearEdge = grassShoreFactor(18.0f, 0.0f, 2.0f, c);
    EXPECT_GT(nearEdge, 0.0f);
    EXPECT_LT(nearEdge, 1.0f);
    EXPECT_FLOAT_EQ(grassShoreFactor(25.0f, 0.0f, 2.0f, c), 0.0f);   // outside the reach
}

// On the mud bank the splat says "no grass"; the shore floor lets reeds grow there
// anyway, and never lowers the weight of ground that is already grass.
TEST(GrassPlacement, ShoreWeightFloorsTheMudBank)
{
    const GrassConfig c = makeShoreConfig();
    EXPECT_FLOAT_EQ(grassShoreWeight(0.0f, 1.0f, c), c.shoreMinGrassWeight);
    EXPECT_FLOAT_EQ(grassShoreWeight(0.0f, 0.0f, c), 0.0f);
    EXPECT_FLOAT_EQ(grassShoreWeight(1.0f, 1.0f, c), 1.0f);
}

// A blade at the waterline is taller and more upright; away from the shore it is untouched.
TEST(GrassPlacement, ShoreBladeIsTallerAndMoreUpright)
{
    const GrassConfig c = makeShoreConfig();
    const GrassBlade meadow = makeGrassBlade(glm::vec3(0.0f), 777u, c);

    GrassBlade reed = meadow;
    applyGrassShore(reed, 1.0f, c);
    EXPECT_FLOAT_EQ(reed.height, meadow.height * c.shoreHeightScale);
    EXPECT_FLOAT_EQ(reed.lean, meadow.lean * c.shoreLeanScale);
    EXPECT_GT(reed.height, meadow.height);
    EXPECT_LT(reed.lean, meadow.lean);

    GrassBlade untouched = meadow;
    applyGrassShore(untouched, 0.0f, c);
    EXPECT_EQ(untouched.height, meadow.height);
    EXPECT_EQ(untouched.lean, meadow.lean);
}
