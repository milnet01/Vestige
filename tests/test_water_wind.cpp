// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_water_wind.cpp
/// @brief Pins the flat-unless-windy ripple gate (waterWindRippleScale) for wind-driven
///        water (the meadow pond): a still mirror (0) in calm air, full ripple (1) when
///        windy, monotonic and continuous in between. Pure — no GL context, no component.

#include "scene/water_surface.h"

#include <gtest/gtest.h>

using namespace Vestige;

// Below and at the calm knot (3 m/s) the surface is a flat mirror — zero ripple.
TEST(WaterWind, FlatMirrorWhenCalm)
{
    EXPECT_FLOAT_EQ(waterWindRippleScale(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(waterWindRippleScale(-5.0f), 0.0f);   // clamps negatives
    EXPECT_FLOAT_EQ(waterWindRippleScale(3.0f), 0.0f);    // at the calm knot
}

// At and above the full knot (16 m/s) the surface shows its full configured ripple.
TEST(WaterWind, FullRippleWhenWindy)
{
    EXPECT_FLOAT_EQ(waterWindRippleScale(16.0f), 1.0f);   // at the full knot
    EXPECT_FLOAT_EQ(waterWindRippleScale(30.0f), 1.0f);   // very windy — saturates
}

// A light breeze between the knots is partial, and strictly inside (0, 1).
TEST(WaterWind, PartialBetweenKnots)
{
    const float mid = waterWindRippleScale(9.5f);         // midpoint of [3, 16]
    EXPECT_GT(mid, 0.0f);
    EXPECT_LT(mid, 1.0f);
    EXPECT_NEAR(mid, 0.5f, 1.0e-5f);                      // smoothstep midpoint = 0.5
}

// The gate never decreases as wind rises (more wind never calms the water).
TEST(WaterWind, MonotonicNonDecreasing)
{
    float prev = waterWindRippleScale(0.0f);
    for (float w = 0.0f; w <= 30.0f; w += 0.25f)
    {
        const float f = waterWindRippleScale(w);
        EXPECT_GE(f, prev - 1.0e-6f) << "ripple gate fell at wind=" << w;
        EXPECT_GE(f, 0.0f);
        EXPECT_LE(f, 1.0f);
        prev = f;
    }
}
