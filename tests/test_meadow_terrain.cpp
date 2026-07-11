// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

// Unit tests for the pure meadow-scene helpers (3D_E-0027, design §5.2 / §11).
// These run headlessly — meadowHeight01 and scatterProps take no GL context,
// no global RNG and no wall-clock, so the scene is a deterministic fixture.

#include <gtest/gtest.h>

#include "environment/meadow_terrain.h"

using namespace Vestige;

namespace
{

MeadowShape makeShape()
{
    MeadowShape s;
    s.baseHeight01 = 0.14f;
    s.octaves = {{1.3f, 0.045f}, {2.7f, 0.025f}, {5.5f, 0.012f}};
    s.pondCenterGrid = {0.5f, 0.44f};
    s.pondRadiusGrid = 0.11f;
    s.bowlDepth01 = 0.09f;
    return s;
}

}  // namespace

// ---- meadowHeight01 --------------------------------------------------------

TEST(MeadowHeight, StaysInUnitRange)
{
    const MeadowShape s = makeShape();
    for (int zi = 0; zi <= 32; ++zi)
    {
        for (int xi = 0; xi <= 32; ++xi)
        {
            const float nx = static_cast<float>(xi) / 32.0f;
            const float nz = static_cast<float>(zi) / 32.0f;
            const float h = meadowHeight01(nx, nz, s);
            EXPECT_GE(h, 0.0f);
            EXPECT_LE(h, 1.0f);
        }
    }
}

TEST(MeadowHeight, Deterministic)
{
    const MeadowShape s = makeShape();
    EXPECT_FLOAT_EQ(meadowHeight01(0.5f, 0.44f, s), meadowHeight01(0.5f, 0.44f, s));
    EXPECT_FLOAT_EQ(meadowHeight01(0.21f, 0.83f, s), meadowHeight01(0.21f, 0.83f, s));
}

TEST(MeadowHeight, BowlFloorLiesBelowTheRim)
{
    const MeadowShape s = makeShape();
    // The bowl centre must dip below a point out at the bowl rim (same azimuth),
    // otherwise the pond would not be contained.
    const float centre = meadowHeight01(s.pondCenterGrid.x, s.pondCenterGrid.y, s);
    const float rim = meadowHeight01(s.pondCenterGrid.x, s.pondCenterGrid.y + s.pondRadiusGrid, s);
    EXPECT_LT(centre, rim);
}

TEST(MeadowHeight, NoBowlOutsideRadius)
{
    // With no octaves and the sample point well outside the bowl, the height is
    // exactly the base offset — the carve must not reach past its radius.
    MeadowShape s;
    s.baseHeight01 = 0.2f;
    s.octaves.clear();
    s.pondCenterGrid = {0.5f, 0.5f};
    s.pondRadiusGrid = 0.1f;
    s.bowlDepth01 = 0.1f;
    EXPECT_FLOAT_EQ(meadowHeight01(0.9f, 0.9f, s), 0.2f);  // far corner, untouched
    EXPECT_LT(meadowHeight01(0.5f, 0.5f, s), 0.2f);        // centre, carved
}

// ---- scatterProps ----------------------------------------------------------

namespace
{

ScatterParams makeScatter()
{
    ScatterParams p;
    p.regionMin = {-100.0f, -100.0f};
    p.regionMax = {100.0f, 100.0f};
    p.cellSize = 12.0f;
    p.jitter = 0.7f;
    p.minDist = 4.0f;
    p.exclusionCenter = {0.0f, -15.0f};
    p.exclusionRadius = 20.0f;
    p.minScale = 0.8f;
    p.maxScale = 1.6f;
    return p;
}

}  // namespace

TEST(ScatterProps, DeterministicForSameSeed)
{
    const ScatterParams p = makeScatter();
    const auto a = scatterProps(1234u, p);
    const auto b = scatterProps(1234u, p);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_FALSE(a.empty());
    for (size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_FLOAT_EQ(a[i].x, b[i].x);
        EXPECT_FLOAT_EQ(a[i].z, b[i].z);
        EXPECT_FLOAT_EQ(a[i].yawDeg, b[i].yawDeg);
        EXPECT_FLOAT_EQ(a[i].scale, b[i].scale);
    }
}

TEST(ScatterProps, DifferentSeedShiftsPoints)
{
    const ScatterParams p = makeScatter();
    const auto a = scatterProps(1u, p);
    const auto b = scatterProps(2u, p);
    ASSERT_FALSE(a.empty());
    // At least one point must move — the two seeds are independent.
    bool anyDifferent = a.size() != b.size();
    for (size_t i = 0; i < a.size() && i < b.size() && !anyDifferent; ++i)
    {
        anyDifferent = a[i].x != b[i].x || a[i].z != b[i].z;
    }
    EXPECT_TRUE(anyDifferent);
}

TEST(ScatterProps, RespectsRegionExclusionDistanceAndRanges)
{
    const ScatterParams p = makeScatter();
    const auto pts = scatterProps(99u, p);
    ASSERT_FALSE(pts.empty());

    const float exclR2 = p.exclusionRadius * p.exclusionRadius;
    for (const auto& pt : pts)
    {
        // In region.
        EXPECT_GE(pt.x, p.regionMin.x);
        EXPECT_LE(pt.x, p.regionMax.x);
        EXPECT_GE(pt.z, p.regionMin.y);
        EXPECT_LE(pt.z, p.regionMax.y);
        // Outside the exclusion disc.
        const float ex = pt.x - p.exclusionCenter.x;
        const float ez = pt.z - p.exclusionCenter.y;
        EXPECT_GE(ex * ex + ez * ez, exclR2);
        // Scale + yaw in range.
        EXPECT_GE(pt.scale, p.minScale);
        EXPECT_LE(pt.scale, p.maxScale);
        EXPECT_GE(pt.yawDeg, 0.0f);
        EXPECT_LT(pt.yawDeg, 360.0f);
    }

    // Min-distance reject: every accepted pair is at least minDist apart.
    const float minD2 = p.minDist * p.minDist;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        for (size_t j = i + 1; j < pts.size(); ++j)
        {
            const float dx = pts[i].x - pts[j].x;
            const float dz = pts[i].z - pts[j].z;
            EXPECT_GE(dx * dx + dz * dz, minD2);
        }
    }
}

TEST(ScatterProps, DegenerateParamsYieldNoPoints)
{
    ScatterParams p = makeScatter();
    p.cellSize = 0.0f;  // invalid spacing
    EXPECT_TRUE(scatterProps(1u, p).empty());

    ScatterParams q = makeScatter();
    q.regionMax = q.regionMin;  // zero-area region
    EXPECT_TRUE(scatterProps(1u, q).empty());
}
