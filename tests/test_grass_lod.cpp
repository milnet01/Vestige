// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_grass_lod.cpp
/// @brief G3 — the pure distance-LOD selector (design §5.3). Pins the segment-tier steps,
///        the continuous `grassKeptFraction` curve (the CPU-instanceCount / GPU-fade seam),
///        its monotonicity, and the distance cull. No GL context, no Terrain.

#include "environment/grass_lod.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

namespace
{
const GrassLodBands kBands;   // defaults: nearMid 45, midFar 95, cull 170, band 24
} // namespace

// Segment tier steps near→mid→far on the chunk's nearest-point distance (§5.3).
TEST(GrassLod, SegmentTierByDistance_G3)
{
    EXPECT_EQ(grassLodForDistance(5.0f, kBands).segments, kBands.nearSegments);    // near
    EXPECT_EQ(grassLodForDistance(44.9f, kBands).segments, kBands.nearSegments);
    EXPECT_EQ(grassLodForDistance(45.0f, kBands).segments, kBands.midSegments);    // mid at boundary
    EXPECT_EQ(grassLodForDistance(94.9f, kBands).segments, kBands.midSegments);
    EXPECT_EQ(grassLodForDistance(95.0f, kBands).segments, kBands.farSegments);    // far at boundary
    EXPECT_EQ(grassLodForDistance(160.0f, kBands).segments, kBands.farSegments);
}

// Beyond the cull distance the chunk is not drawn; just inside it is.
TEST(GrassLod, DistanceCull_G3)
{
    EXPECT_TRUE(grassLodForDistance(169.9f, kBands).draw);
    EXPECT_FALSE(grassLodForDistance(170.0f, kBands).draw);
    EXPECT_FALSE(grassLodForDistance(500.0f, kBands).draw);
    EXPECT_FLOAT_EQ(grassLodForDistance(170.0f, kBands).instanceFraction, 0.0f);
}

// The kept fraction hits the tier levels at the band edges (the values the CPU and shader
// must agree on) and stays within [farFraction, nearFraction].
TEST(GrassLod, KeptFractionAtBandEdges_G3)
{
    // Before the first band: full near fraction.
    EXPECT_FLOAT_EQ(grassKeptFraction(kBands.nearMid - kBands.bandWidth, kBands), kBands.nearFraction);
    EXPECT_FLOAT_EQ(grassKeptFraction(0.0f, kBands), kBands.nearFraction);
    // At the near→mid boundary: mid fraction.
    EXPECT_FLOAT_EQ(grassKeptFraction(kBands.nearMid, kBands), kBands.midFraction);
    // Through the mid tier flat, then far fraction at the mid→far boundary.
    EXPECT_FLOAT_EQ(grassKeptFraction(kBands.midFar - kBands.bandWidth, kBands), kBands.midFraction);
    EXPECT_FLOAT_EQ(grassKeptFraction(kBands.midFar, kBands), kBands.farFraction);
    EXPECT_FLOAT_EQ(grassKeptFraction(160.0f, kBands), kBands.farFraction);
}

// grassLodForDistance's instanceFraction is exactly grassKeptFraction (same seam value).
TEST(GrassLod, InstanceFractionEqualsKeptFraction_G3)
{
    for (float d = 0.0f; d < kBands.cullDist; d += 3.7f)
    {
        EXPECT_FLOAT_EQ(grassLodForDistance(d, kBands).instanceFraction,
                        grassKeptFraction(d, kBands));
    }
}

// The kept-fraction curve is monotonic non-increasing — more distance never grows the
// field. (No-pop rests on the fade only ever removing blades as you recede, §5.3.)
TEST(GrassLod, KeptFractionMonotonicNonIncreasing_G3)
{
    float prev = grassKeptFraction(0.0f, kBands);
    for (float d = 0.0f; d <= kBands.cullDist; d += 0.5f)
    {
        const float f = grassKeptFraction(d, kBands);
        EXPECT_LE(f, prev + 1.0e-6f) << "kept fraction rose at d=" << d;
        EXPECT_GE(f, kBands.farFraction - 1.0e-6f);
        EXPECT_LE(f, kBands.nearFraction + 1.0e-6f);
        prev = f;
    }
}

// The curve is continuous (smoothstep, no jumps) — sample tightly and bound the step.
TEST(GrassLod, KeptFractionContinuous_G3)
{
    float prev = grassKeptFraction(0.0f, kBands);
    for (float d = 0.0f; d <= kBands.cullDist; d += 0.05f)
    {
        const float f = grassKeptFraction(d, kBands);
        EXPECT_LT(std::abs(f - prev), 0.02f) << "kept fraction jumped at d=" << d;
        prev = f;
    }
}
