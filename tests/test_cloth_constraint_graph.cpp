// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_constraint_graph.cpp
/// @brief Unit tests for cloth constraint generation and graph colouring.
///
/// These cover the CPU-side helpers that build the GPU constraint graph
/// for `GpuClothSimulator`. The colouring contract is the load-bearing
/// invariant: within a single colour, no two constraints may share a
/// particle, otherwise the GPU's per-colour parallel solve would race.

#include <gtest/gtest.h>

#include "physics/cloth_constraint_graph.h"

#include <set>
#include <utility>

using namespace Vestige;

namespace
{

// Builds a flat W×H grid of particles, spacing 1 m, centered at origin.
std::vector<glm::vec3> makeFlatGrid(uint32_t W, uint32_t H, float spacing = 1.0f)
{
    std::vector<glm::vec3> p(W * H);
    const float wMinus1 = static_cast<float>(W - 1);
    const float hMinus1 = static_cast<float>(H - 1);
    for (uint32_t z = 0; z < H; ++z)
    {
        for (uint32_t x = 0; x < W; ++x)
        {
            const float fx = (static_cast<float>(x) - wMinus1 * 0.5f) * spacing;
            const float fz = (static_cast<float>(z) - hMinus1 * 0.5f) * spacing;
            p[z * W + x] = glm::vec3(fx, 0.0f, fz);
        }
    }
    return p;
}

} // namespace

// -- generateGridConstraints --

TEST(ClothConstraintGraph, GeneratesExpectedStretchShearBendCounts)
{
    constexpr uint32_t W = 4, H = 4;
    auto positions = makeFlatGrid(W, H);

    std::vector<GpuConstraint> cs;
    generateGridConstraints(W, H, positions, 0.0f, 0.0001f, /*bend=*/0.01f, cs);

    // Stretch: 2 * W * H - W - H = 32 - 8 = 24.
    // Shear:   2 * (W-1) * (H-1) = 18.
    // Bend:    (W-2) * H + W * (H-2) = 8 + 8 = 16.
    EXPECT_EQ(cs.size(), 24u + 18u + 16u);
}

TEST(ClothConstraintGraph, BendConstraintsHaveSkipOneRestLength)
{
    constexpr uint32_t W = 5, H = 5;
    constexpr float spacing = 1.0f;
    auto positions = makeFlatGrid(W, H, spacing);

    std::vector<GpuConstraint> cs;
    generateGridConstraints(W, H, positions, 0.0f, 0.0f, /*bend=*/0.01f, cs);

    // Find any bend constraint by its compliance value (uniqueness in this fixture)
    // and verify its rest length is 2 * spacing — the skip-one distance.
    bool foundBend = false;
    for (const auto& c : cs)
    {
        if (c.compliance == 0.01f)
        {
            EXPECT_NEAR(c.restLength, 2.0f * spacing, 1e-5f)
                << "bend constraint " << c.i0 << "-" << c.i1
                << " should span two grid cells";
            foundBend = true;
        }
    }
    EXPECT_TRUE(foundBend) << "expected at least one bend constraint on a 5x5 grid";
}

TEST(ClothConstraintGraph, NoBendConstraintsForGridSmallerThanThree)
{
    // Bend edges are skip-one — they require at least 3 particles along an axis.
    // A 2xN grid should produce only stretch + shear, never bend.
    constexpr uint32_t W = 2, H = 4;
    auto positions = makeFlatGrid(W, H);
    std::vector<GpuConstraint> cs;
    generateGridConstraints(W, H, positions, 0.0f, 0.0f, /*bend=*/0.01f, cs);

    // Stretch X: (W-1)*H = 4. Stretch Z: W*(H-1) = 6. Total stretch = 10.
    // Shear:     2*(W-1)*(H-1) = 6.
    // Bend X:    needs W >= 3 → 0.
    // Bend Z:    W * (H-2) = 4.
    EXPECT_EQ(cs.size(), 10u + 6u + 4u);
}

TEST(ClothConstraintGraph, RestLengthMatchesEuclideanDistance)
{
    constexpr uint32_t W = 3, H = 3;
    auto positions = makeFlatGrid(W, H, /*spacing=*/2.0f);

    std::vector<GpuConstraint> cs;
    generateGridConstraints(W, H, positions, 0.0f, 0.0f, /*bend=*/0.0f, cs);

    // First constraint should be the (0,0)–(1,0) horizontal edge, rest = 2.0f.
    ASSERT_FALSE(cs.empty());
    EXPECT_EQ(cs[0].i0, 0u);
    EXPECT_EQ(cs[0].i1, 1u);
    EXPECT_FLOAT_EQ(cs[0].restLength, 2.0f);
}

TEST(ClothConstraintGraph, NoConstraintsForDegenerateGrid)
{
    auto positions = makeFlatGrid(1, 1);
    std::vector<GpuConstraint> cs;
    generateGridConstraints(1, 1, positions, 0.0f, 0.0f, /*bend=*/0.0f, cs);
    EXPECT_TRUE(cs.empty());
}

TEST(ClothConstraintGraph, AppendsToExistingBuffer)
{
    constexpr uint32_t W = 3, H = 3;
    auto positions = makeFlatGrid(W, H);
    std::vector<GpuConstraint> cs;
    cs.push_back(GpuConstraint{99, 100, 1.0f, 0.0f});  // sentinel
    generateGridConstraints(W, H, positions, 0.0f, 0.0001f, /*bend=*/0.01f, cs);
    EXPECT_GT(cs.size(), 1u);
    EXPECT_EQ(cs[0].i0, 99u) << "existing entries must be preserved";
}

// -- colourConstraints (the load-bearing invariant) --

TEST(ClothConstraintGraph, ColouringHasNoSharedParticleWithinColour)
{
    // The whole point of the colouring: for every colour, the constraints
    // assigned to it must be a particle-disjoint set so the GPU can solve
    // them all in parallel without stepping on each other's writes.
    constexpr uint32_t W = 8, H = 8;
    auto positions = makeFlatGrid(W, H);

    std::vector<GpuConstraint> cs;
    generateGridConstraints(W, H, positions, 0.0f, 0.0001f, /*bend=*/0.01f, cs);
    auto ranges = colourConstraints(cs, W * H);

    ASSERT_FALSE(ranges.empty());
    for (const auto& r : ranges)
    {
        std::set<uint32_t> particles;
        for (uint32_t k = 0; k < r.count; ++k)
        {
            const auto& c = cs[r.offset + k];
            EXPECT_TRUE(particles.insert(c.i0).second)
                << "colour " << (&r - ranges.data()) << " sees i0=" << c.i0 << " twice";
            EXPECT_TRUE(particles.insert(c.i1).second)
                << "colour " << (&r - ranges.data()) << " sees i1=" << c.i1 << " twice";
        }
    }
}

TEST(ClothConstraintGraph, ColouringIsConservativeForRegularGrid)
{
    // A regular grid with stretch + shear + bend has max degree 12 at
    // interior particles (4 stretch + 4 shear + 4 bend edges each), so the
    // greedy bound is Δ+1 = 13 colours in the worst case. Sanity-cap at 16
    // to leave a little headroom but still flag a real algorithmic regression.
    constexpr uint32_t W = 16, H = 16;
    auto positions = makeFlatGrid(W, H);

    std::vector<GpuConstraint> cs;
    generateGridConstraints(W, H, positions, 0.0f, 0.0001f, /*bend=*/0.01f, cs);
    auto ranges = colourConstraints(cs, W * H);

    EXPECT_LE(ranges.size(), 16u)
        << "greedy colouring produced an unexpectedly high colour count";
    EXPECT_GE(ranges.size(), 4u)
        << "regular grid should need at least 4 colours";
}

TEST(ClothConstraintGraph, RangesPartitionTheConstraintArray)
{
    // The sum of all colour counts must equal the total constraint count,
    // and the offsets must form a contiguous tiling — no gaps, no overlap.
    constexpr uint32_t W = 6, H = 6;
    auto positions = makeFlatGrid(W, H);

    std::vector<GpuConstraint> cs;
    generateGridConstraints(W, H, positions, 0.0f, 0.0001f, /*bend=*/0.01f, cs);
    const size_t totalBefore = cs.size();
    auto ranges = colourConstraints(cs, W * H);

    EXPECT_EQ(cs.size(), totalBefore) << "colouring must not lose constraints";

    uint32_t expectedOffset = 0;
    uint32_t totalCount = 0;
    for (const auto& r : ranges)
    {
        EXPECT_EQ(r.offset, expectedOffset) << "ranges must be contiguous";
        expectedOffset += r.count;
        totalCount += r.count;
    }
    EXPECT_EQ(totalCount, static_cast<uint32_t>(cs.size()));
}

TEST(ClothConstraintGraph, EmptyConstraintsYieldEmptyRanges)
{
    std::vector<GpuConstraint> cs;
    auto ranges = colourConstraints(cs, /*particleCount=*/16);
    EXPECT_TRUE(ranges.empty());
}
