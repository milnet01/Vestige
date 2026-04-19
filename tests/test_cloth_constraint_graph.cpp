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

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

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

// -- Dihedral generation + colouring (Step 6) --

namespace
{

// Build the same triangle index buffer that GpuClothSimulator's grid builder
// produces, so dihedral edge-walking sees a manifold mesh identical to what
// the runtime feeds it.
std::vector<uint32_t> makeGridIndices(uint32_t W, uint32_t H)
{
    std::vector<uint32_t> idx;
    idx.reserve((W - 1) * (H - 1) * 6);
    for (uint32_t z = 0; z + 1 < H; ++z)
    {
        for (uint32_t x = 0; x + 1 < W; ++x)
        {
            const uint32_t i0 = z * W + x;
            const uint32_t i1 = z * W + (x + 1);
            const uint32_t i2 = (z + 1) * W + x;
            const uint32_t i3 = (z + 1) * W + (x + 1);
            idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
            idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
        }
    }
    return idx;
}

} // namespace

TEST(ClothConstraintGraph, DihedralCountMatchesAnalyticalFormula)
{
    // For an M×N quad grid (M=W-1, N=H-1) using the diagonal triangulation,
    // dihedrals = 3*M*N - M - N (one per cell-internal diagonal + one per
    // shared horizontal edge + one per shared vertical edge between cells).
    constexpr uint32_t W = 4, H = 4;
    auto positions = makeFlatGrid(W, H);
    auto indices   = makeGridIndices(W, H);

    std::vector<GpuDihedralConstraint> ds;
    generateDihedralConstraints(indices, positions, /*compliance=*/0.01f, ds);

    constexpr uint32_t M = W - 1;
    constexpr uint32_t N = H - 1;
    constexpr uint32_t expected = 3 * M * N - M - N;
    EXPECT_EQ(ds.size(), expected);
}

TEST(ClothConstraintGraph, DihedralRestAngleIsZeroForFlatGrid)
{
    // A flat grid has all triangles coplanar → every dihedral rest angle == 0.
    constexpr uint32_t W = 5, H = 5;
    auto positions = makeFlatGrid(W, H);
    auto indices   = makeGridIndices(W, H);

    std::vector<GpuDihedralConstraint> ds;
    generateDihedralConstraints(indices, positions, /*compliance=*/0.01f, ds);
    ASSERT_FALSE(ds.empty());
    for (const auto& d : ds)
    {
        EXPECT_NEAR(d.restAngle, 0.0f, 1e-4f);
    }
}

TEST(ClothConstraintGraph, DihedralEmptyForSingleTriangle)
{
    // One triangle has no shared edges → no dihedrals.
    std::vector<glm::vec3> positions = {
        {0, 0, 0}, {1, 0, 0}, {0, 0, 1}
    };
    std::vector<uint32_t> indices = {0, 1, 2};
    std::vector<GpuDihedralConstraint> ds;
    generateDihedralConstraints(indices, positions, 0.01f, ds);
    EXPECT_TRUE(ds.empty());
}

TEST(ClothConstraintGraph, DihedralColouringHasNoSharedParticleWithinColour)
{
    // The load-bearing invariant for the GPU pass: within a colour, the
    // four-particle endpoint sets of any two dihedrals must be disjoint.
    constexpr uint32_t W = 6, H = 6;
    auto positions = makeFlatGrid(W, H);
    auto indices   = makeGridIndices(W, H);

    std::vector<GpuDihedralConstraint> ds;
    generateDihedralConstraints(indices, positions, 0.01f, ds);
    auto ranges = colourDihedralConstraints(ds, W * H);

    ASSERT_FALSE(ranges.empty());
    for (const auto& r : ranges)
    {
        std::set<uint32_t> seen;
        for (uint32_t k = 0; k < r.count; ++k)
        {
            const auto& d = ds[r.offset + k];
            EXPECT_TRUE(seen.insert(d.p0).second);
            EXPECT_TRUE(seen.insert(d.p1).second);
            EXPECT_TRUE(seen.insert(d.p2).second);
            EXPECT_TRUE(seen.insert(d.p3).second);
        }
    }
}

TEST(ClothConstraintGraph, DihedralRangesPartitionTheConstraintArray)
{
    constexpr uint32_t W = 5, H = 5;
    auto positions = makeFlatGrid(W, H);
    auto indices   = makeGridIndices(W, H);

    std::vector<GpuDihedralConstraint> ds;
    generateDihedralConstraints(indices, positions, 0.01f, ds);
    const size_t before = ds.size();
    auto ranges = colourDihedralConstraints(ds, W * H);

    EXPECT_EQ(ds.size(), before);
    uint32_t expectedOffset = 0;
    uint32_t total = 0;
    for (const auto& r : ranges)
    {
        EXPECT_EQ(r.offset, expectedOffset);
        expectedOffset += r.count;
        total += r.count;
    }
    EXPECT_EQ(total, static_cast<uint32_t>(ds.size()));
}

TEST(ClothConstraintGraph, DihedralStructIsThirtyTwoBytes)
{
    // The std430 layout uses uvec4 + vec4 → 32 B. Pin so a refactor that
    // removes the padding silently breaks GPU upload alignment.
    EXPECT_EQ(sizeof(GpuDihedralConstraint), 32u);
}
