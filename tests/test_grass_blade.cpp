// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_grass_blade.cpp
/// @brief G1 — Rule-7 parity + invariants for the GL-free grass blade-vertex mirror
///        (design docs/phases/phase_10_meadow_gpu_grass_design.md §5.1/§8).
///
/// `grassBladeVertex` is the CPU hand-mirror of `grass.vert.glsl`'s Bézier-ribbon
/// generator. These tests pin the STATIC blade contract the GLSL must also honour:
/// the strip endpoints sit on the curve (row 0 = root/P0, tip = P2), the width tapers
/// monotonically to ~0 at the tip, the base pair spans the seed width, and the tip is a
/// single point. No GL context — pure math, like the terrain_material_blend parity tests.

#include "environment/grass_blade.h"

#include <gtest/gtest.h>

#include <glm/geometric.hpp>

using namespace Vestige;

namespace
{
// A deliberately non-trivial seed (off-origin root, non-axis facing) so a formula
// divergence in the GLSL can't hide behind a symmetric special case.
GrassBlade makeSeed()
{
    GrassBlade b;
    b.rootPos = glm::vec3(2.0f, 0.5f, -3.0f);
    b.height = 1.2f;
    b.facingAngle = 0.7f;
    b.lean = 0.3f;
    b.width = 0.08f;
    b.hash = 0u;
    return b;
}

constexpr int N = 7;  // near-LOD segment count → 2N+1 = 15 strip verts.

void expectVec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f)
{
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}
} // namespace

// The std430 packing contract G1's Verify depends on (design §5.5).
TEST(GrassBlade, SeedIsThirtyTwoBytes_G1)
{
    EXPECT_EQ(sizeof(GrassBlade), 32u);
}

// Bézier passes through its endpoints (P0 at t=0, P2 at t=1) — not P1.
TEST(GrassBlade, BezierHitsEndpointsNotMidControl_G1)
{
    const glm::vec3 p0(0.0f), p1(0.0f, 1.0f, 0.0f), p2(0.3f, 1.0f, 0.0f);
    expectVec3Near(grassBezier(p0, p1, p2, 0.0f), p0);
    expectVec3Near(grassBezier(p0, p1, p2, 1.0f), p2);
    // At t=0.5 the curve is pulled toward P1 but does NOT reach it.
    const glm::vec3 mid = grassBezier(p0, p1, p2, 0.5f);
    EXPECT_GT(glm::length(mid - p1), 0.1f);
}

// Row 0 is the blade base: its left/right midpoint sits exactly on the curve at P0 = root.
TEST(GrassBlade, BaseRowMidpointIsRoot_G1)
{
    const GrassBlade b = makeSeed();
    const glm::vec3 left = grassBladeVertex(b, 0, -1, N);
    const glm::vec3 right = grassBladeVertex(b, 0, +1, N);
    expectVec3Near((left + right) * 0.5f, b.rootPos);
}

// The base pair spans exactly the seed's full width.
TEST(GrassBlade, BaseRowSpansSeedWidth_G1)
{
    const GrassBlade b = makeSeed();
    const glm::vec3 left = grassBladeVertex(b, 0, -1, N);
    const glm::vec3 right = grassBladeVertex(b, 0, +1, N);
    EXPECT_NEAR(glm::length(right - left), b.width, 1e-5f);
}

// The tip row is a single centred point at P2 — `side` must not move it (width→0).
TEST(GrassBlade, TipIsSinglePointAtP2_G1)
{
    const GrassBlade b = makeSeed();
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 dir(std::cos(b.facingAngle), 0.0f, std::sin(b.facingAngle));
    const glm::vec3 p2 = b.rootPos + up * b.height + dir * (b.height * b.lean);

    const glm::vec3 tipL = grassBladeVertex(b, N, -1, N);
    const glm::vec3 tipR = grassBladeVertex(b, N, +1, N);
    expectVec3Near(tipL, tipR);   // side is a no-op at the tip
    expectVec3Near(tipL, p2);     // and it sits on the curve endpoint
}

// Width tapers strictly monotonically down the blade, reaching ~0 at the tip.
TEST(GrassBlade, WidthTapersMonotonicallyToZero_G1)
{
    const GrassBlade b = makeSeed();
    float prev = grassBladeWidth(b.width, 0.0f);
    EXPECT_NEAR(prev, b.width, 1e-6f);
    for (int row = 1; row <= N; ++row)
    {
        const float t = static_cast<float>(row) / static_cast<float>(N);
        const float w = grassBladeWidth(b.width, t);
        EXPECT_LT(w, prev);       // strictly decreasing
        prev = w;
    }
    EXPECT_NEAR(prev, 0.0f, 1e-6f);  // ~0 at the tip
}

// The generator honours the 2N+1 strip layout: rows advance up the curve monotonically
// in height (the blade grows upward from base to tip).
TEST(GrassBlade, RowsAscendFromBaseToTip_G1)
{
    const GrassBlade b = makeSeed();
    float prevY = grassBladeVertex(b, 0, -1, N).y;
    for (int row = 1; row <= N; ++row)
    {
        const float y = grassBladeVertex(b, row, -1, N).y;
        EXPECT_GT(y, prevY);  // upward Bézier with a positive-height P1
        prevY = y;
    }
}
