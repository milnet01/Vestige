// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_grass_clump.cpp
/// @brief G2 — properties of the GL-free clump field `grassClump` (design
///        docs/phases/phase_10_meadow_gpu_grass_design.md §5.2a/§8).
///
/// The clump field is the "field, not tufts" layer and the CPU mirror of grass.vert.glsl.
/// These tests pin the contract the GLSL must also honour: determinism, factor ranges,
/// **C⁰ continuity of the scalar factors over the covered field** (incl. cell corners /
/// Voronoi triple points — where the earlier two-nearest blend jumped), and **finiteness
/// everywhere** including the synthetic degenerate input that forces the Σwᵢ<ε fallback.
/// No GL — pure math, like the terrain/blade parity tests.

#include "environment/grass_clump.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

namespace
{
constexpr float CELL = 1.0f;

bool isFinite(const GrassClump& c)
{
    return std::isfinite(c.height) && std::isfinite(c.leanDir.x) && std::isfinite(c.leanDir.y)
        && std::isfinite(c.tint) && std::isfinite(c.bend) && std::isfinite(c.phase);
}
} // namespace

// Same world XZ → identical result (the CPU/GPU parity contract rests on determinism).
TEST(GrassClump, Deterministic_G2)
{
    const GrassClumpParams p = defaultClumpParams(CELL);
    const GrassClump a = grassClump(3.7f, -2.1f, p);
    const GrassClump b = grassClump(3.7f, -2.1f, p);
    EXPECT_EQ(a.height, b.height);
    EXPECT_EQ(a.leanDir.x, b.leanDir.x);
    EXPECT_EQ(a.leanDir.y, b.leanDir.y);
    EXPECT_EQ(a.tint, b.tint);
    EXPECT_EQ(a.bend, b.bend);
    EXPECT_EQ(a.phase, b.phase);
}

// Blended factors stay in their design ranges, and leanDir stays unit-length — across a
// dense grid that spans negative coordinates (floor, not truncation).
TEST(GrassClump, FactorsInRange_G2)
{
    const GrassClumpParams p = defaultClumpParams(CELL);
    for (float z = -3.0f; z <= 3.0f; z += 0.05f)
    {
        for (float x = -3.0f; x <= 3.0f; x += 0.05f)
        {
            const GrassClump c = grassClump(x, z, p);
            ASSERT_TRUE(isFinite(c)) << "NaN at (" << x << "," << z << ")";
            // Convex blend of per-cell heights ∈ [0.6, 1.6] stays in range.
            EXPECT_GE(c.height, 0.6f - 1e-4f);
            EXPECT_LE(c.height, 1.6f + 1e-4f);
            EXPECT_GE(c.tint, -1.0f - 1e-4f);
            EXPECT_LE(c.tint, 1.0f + 1e-4f);
            EXPECT_GE(c.bend, 0.0f - 1e-4f);
            EXPECT_LE(c.bend, 1.0f + 1e-4f);
            EXPECT_GE(c.phase, 0.0f);
            EXPECT_LE(c.phase, 6.2831854f);
            EXPECT_NEAR(glm::length(c.leanDir), 1.0f, 1e-4f);
        }
    }
}

// C⁰: over the covered field (committed envelope), the scalar factors never jump — the
// step between adjacent samples stays small even across cell boundaries, corners, and
// Voronoi triple points. A discontinuity (the old two-nearest bug) would show as an
// O(1) jump; a smooth field keeps the step O(stepSize).
TEST(GrassClump, ScalarFactorsAreC0_G2)
{
    const GrassClumpParams p = defaultClumpParams(CELL);
    const float step = 0.01f;
    // Lipschitz-style bound: factors are O(1), kernel radius is cellSize=1, so the
    // per-step change is bounded well under this. A real seam would be ~0.3+.
    const float maxStep = 0.08f;

    float worstH = 0.0f, worstT = 0.0f, worstB = 0.0f;
    for (float z = -2.0f; z <= 2.0f; z += step)
    {
        GrassClump prev = grassClump(-2.0f, z, p);
        for (float x = -2.0f + step; x <= 2.0f; x += step)
        {
            const GrassClump cur = grassClump(x, z, p);
            worstH = std::max(worstH, std::fabs(cur.height - prev.height));
            worstT = std::max(worstT, std::fabs(cur.tint - prev.tint));
            worstB = std::max(worstB, std::fabs(cur.bend - prev.bend));
            prev = cur;
        }
    }
    EXPECT_LT(worstH, maxStep) << "height discontinuity (seam) detected";
    EXPECT_LT(worstT, maxStep) << "tint discontinuity (seam) detected";
    EXPECT_LT(worstB, maxStep) << "bend discontinuity (seam) detected";
}

// Two samples very close together are nearly identical (a direct continuity probe at a
// cell corner, the worst case for the blend).
TEST(GrassClump, ContinuousAtCellCorner_G2)
{
    const GrassClumpParams p = defaultClumpParams(CELL);
    // A cell corner sits at integer world coords (cell size 1).
    const GrassClump a = grassClump(1.0f - 1e-3f, 1.0f - 1e-3f, p);
    const GrassClump b = grassClump(1.0f + 1e-3f, 1.0f + 1e-3f, p);
    EXPECT_NEAR(a.height, b.height, 0.02f);
    EXPECT_NEAR(a.tint, b.tint, 0.02f);
    EXPECT_NEAR(a.bend, b.bend, 0.02f);
}

// The Σwᵢ<ε fallback is unreachable in the committed envelope but must be finite when
// forced: shrink kernelR far below the coverage floor so a corner sample gets all-zero
// weights → the nearest-cell fallback fires and returns a finite (non-NaN) result.
TEST(GrassClump, DegenerateKernelFallbackIsFinite_G2)
{
    GrassClumpParams p = defaultClumpParams(CELL);
    p.kernelR = 0.05f;   // << coverage floor (~0.957·cellSize) → forces all-zero weights
    // Sample near a cell corner, far from every jittered centre.
    for (float z = -2.0f; z <= 2.0f; z += 0.9999f)   // land near integer corners
    {
        for (float x = -2.0f; x <= 2.0f; x += 0.9999f)
        {
            const GrassClump c = grassClump(x, z, p);
            ASSERT_TRUE(isFinite(c)) << "fallback produced NaN at (" << x << "," << z << ")";
            EXPECT_NEAR(glm::length(c.leanDir), 1.0f, 1e-4f);   // still a unit dir
        }
    }
}

// The integer hash is deterministic and spreads: distinct cells give distinct ids for a
// large sample (no catastrophic collisions), and the same cell is stable.
TEST(GrassClump, HashDeterministicAndSpread_G2)
{
    EXPECT_EQ(grassCellHash(5, -9, 0u), grassCellHash(5, -9, 0u));   // stable
    int collisions = 0;
    std::uint32_t prev = grassCellHash(0, 0, 0u);
    for (int i = 1; i < 1000; ++i)
    {
        const std::uint32_t h = grassCellHash(i, -i, 0u);
        if (h == prev) ++collisions;
        prev = h;
    }
    EXPECT_EQ(collisions, 0);
}
