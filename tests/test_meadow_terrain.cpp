// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

// Unit tests for the pure meadow-scene helpers (3D_E-0027, design §5.2 / §11).
// These run headlessly — meadowHeight01 and scatterProps take no GL context,
// no global RNG and no wall-clock, so the scene is a deterministic fixture.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

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

TEST(MeadowHeight, BowlCarvesTheCentreDown)
{
    // The bowl must lower the pond centre relative to the same point with no
    // bowl. (Comparing two *spatial* points is not robust once the relief is
    // noise-based — their noise values have no guaranteed ordering — so we hold
    // the position fixed and toggle the carve.)
    const MeadowShape withBowl = makeShape();
    MeadowShape noBowl = makeShape();
    noBowl.bowlDepth01 = 0.0f;
    const float carved = meadowHeight01(withBowl.pondCenterGrid.x, withBowl.pondCenterGrid.y, withBowl);
    const float flat = meadowHeight01(noBowl.pondCenterGrid.x, noBowl.pondCenterGrid.y, noBowl);
    EXPECT_LT(carved, flat);
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

// ---- computePondFill (design §7.1/§7.2) ------------------------------------
//
// The pond fill is validated in WORLD units against the same meadow bowl the
// scene ships. The terrain is 257² at 1 m spacing, origin (-128,-128), 50 m
// height scale (terrain_system.cpp) — so the test wraps meadowHeight01 (grid
// [0,1] → height01 [0,1]) into a world sampler, exactly as the scene wraps
// Terrain::getHeight (design §3 helper note).

namespace
{

constexpr int   TERRAIN_VERTS = 257;
constexpr float TERRAIN_SPACING = 1.0f;
constexpr float TERRAIN_ORIGIN = -128.0f;
constexpr float HEIGHT_SCALE = 50.0f;
constexpr float WORLD_SPAN = static_cast<float>(TERRAIN_VERTS - 1) * TERRAIN_SPACING;  // 256 m

// World-XZ → world height, wrapping meadowHeight01 (no vertical origin offset —
// origin.y = 0, matching Terrain::getHeight).
struct WorldBowl
{
    MeadowShape shape;
    float operator()(float wx, float wz) const
    {
        const float nx = (wx - TERRAIN_ORIGIN) / WORLD_SPAN;
        const float nz = (wz - TERRAIN_ORIGIN) / WORLD_SPAN;
        return meadowHeight01(nx, nz, shape) * HEIGHT_SCALE;
    }
};

glm::vec2 pondCentreWorld(const MeadowShape& s)
{
    return {TERRAIN_ORIGIN + s.pondCenterGrid.x * WORLD_SPAN,
            TERRAIN_ORIGIN + s.pondCenterGrid.y * WORLD_SPAN};
}

// Independent 4-neighbour flood-fill from the pond centre over a fine grid out
// to rScan. Returns whether water at `waterY` reaches the R_SCAN boundary
// (escape) and the max centre-distance of any flooded cell. This is
// algorithmically independent of computePondFill's ray march (design §7.1).
struct FloodResult { bool escapes; float maxDist; };

template <typename Sampler>
FloodResult floodFill(const Sampler& sample, glm::vec2 centre, float waterY,
                      float rScan, float gridStep)
{
    const int N = static_cast<int>(std::ceil(rScan / gridStep));
    const int side = 2 * N + 1;
    std::vector<bool> visited(static_cast<size_t>(side) * static_cast<size_t>(side), false);
    const auto idx = [&](int i, int j) {
        return static_cast<size_t>((j + N)) * static_cast<size_t>(side) + static_cast<size_t>(i + N);
    };
    std::vector<std::pair<int, int>> stack;
    FloodResult res{false, 0.0f};

    // Seed the centre cell (the floor is below waterY by construction).
    if (sample(centre.x, centre.y) < waterY)
    {
        visited[idx(0, 0)] = true;
        stack.push_back({0, 0});
    }
    const int dI[4] = {1, -1, 0, 0};
    const int dJ[4] = {0, 0, 1, -1};
    while (!stack.empty())
    {
        const auto [i, j] = stack.back();
        stack.pop_back();
        if (std::abs(i) >= N || std::abs(j) >= N)
        {
            res.escapes = true;  // a flooded cell sits on the R_SCAN boundary
        }
        const float dist = std::sqrt(static_cast<float>(i * i + j * j)) * gridStep;
        res.maxDist = std::max(res.maxDist, dist);
        for (int k = 0; k < 4; ++k)
        {
            const int ni = i + dI[k];
            const int nj = j + dJ[k];
            if (std::abs(ni) > N || std::abs(nj) > N || visited[idx(ni, nj)])
            {
                continue;
            }
            const float wx = centre.x + static_cast<float>(ni) * gridStep;
            const float wz = centre.y + static_cast<float>(nj) * gridStep;
            if (sample(wx, wz) < waterY)
            {
                visited[idx(ni, nj)] = true;
                stack.push_back({ni, nj});
            }
        }
    }
    return res;
}

}  // namespace

TEST(PondFill, ContainedAndCoversTheFlood)
{
    const WorldBowl bowl{makeShape()};
    const glm::vec2 centre = pondCentreWorld(bowl.shape);
    const float rRim = bowl.shape.pondRadiusGrid * WORLD_SPAN;   // ≈ 28 m
    const float floorY = bowl(centre.x, centre.y);

    PondFillParams params;
    const PondFill fill = computePondFill(bowl, centre, rRim, floorY, params);

    // By-construction sanity (labelled as such — cannot fail unless arithmetic
    // is broken): the fill sits below the spill and above the floor.
    EXPECT_LT(fill.waterLevelY, fill.spillHeight);
    EXPECT_GE(fill.waterLevelY, floorY + params.minDepth - 1e-3f);

    // §7.2 flood-radius sanity: contained within the bowl, not capped at R_SCAN.
    const float rScan = params.scanFactor * rRim;
    EXPECT_GT(fill.floodRadius, 0.0f);
    EXPECT_LT(fill.floodRadius, rRim);
    EXPECT_LT(fill.floodRadius, rScan);

    // §7.1 INV-2 containment — INDEPENDENT flood-fill (not the ray march):
    // water at waterLevelY cannot reach the R_SCAN boundary.
    const FloodResult flood = floodFill(bowl, centre, fill.waterLevelY, rScan, 1.0f);
    EXPECT_FALSE(flood.escapes) << "pond spills — water reaches the R_SCAN boundary";

    // §7.1 INV-3 coverage — the headline "no straight edge on water": every
    // flooded cell lies inside the computed square sheet.
    const float halfSheet = fill.floodRadius + params.edgePad;  // = POND_SIZE / 2
    EXPECT_LE(flood.maxDist, halfSheet)
        << "sheet undersized — flooded ground extends past POND_SIZE/2";

    // Cross-check the two independent shoreline measures agree within a few cells.
    EXPECT_NEAR(fill.floodRadius, flood.maxDist, 3.0f);
}

TEST(PondFill, DeterministicAndRimRaisesLevel)
{
    const WorldBowl bowl{makeShape()};
    const glm::vec2 centre = pondCentreWorld(bowl.shape);
    const float rRim = bowl.shape.pondRadiusGrid * WORLD_SPAN;
    const float floorY = bowl(centre.x, centre.y);
    PondFillParams params;

    const PondFill a = computePondFill(bowl, centre, rRim, floorY, params);
    const PondFill b = computePondFill(bowl, centre, rRim, floorY, params);
    EXPECT_FLOAT_EQ(a.waterLevelY, b.waterLevelY);
    EXPECT_FLOAT_EQ(a.floodRadius, b.floodRadius);

    // The pond fills above its floor (a real, non-degenerate body of water).
    EXPECT_GT(a.waterLevelY, floorY);
}
