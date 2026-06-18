// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_volumetric_fog.cpp
/// @brief Phase 10 slice 11.6 — froxel-grid coordinate math: exponential
///        depth-slice distribution, its inverse, and screen-UV tiling.
///        These pin the GPU compute shaders (CLAUDE.md Rule 7).

#include <gtest/gtest.h>

#include "renderer/volumetric_fog.h"

#include <cmath>

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;

FroxelGridConfig defaultGrid()
{
    return FroxelGridConfig{}; // 160 x 90 x 64, near=0.5, far=200
}
} // namespace

// ---------------------------------------------------------------------------
// froxelCount
// ---------------------------------------------------------------------------

TEST(VolumetricFog, FroxelCountIsProductOfResolution)
{
    EXPECT_EQ(froxelCount(defaultGrid()), 160 * 90 * 64);
}

TEST(VolumetricFog, FroxelCountZeroForDegenerateResolution)
{
    FroxelGridConfig g = defaultGrid();
    g.resX = 0;
    EXPECT_EQ(froxelCount(g), 0);
    g = defaultGrid();
    g.resZ = -4;
    EXPECT_EQ(froxelCount(g), 0);
}

// ---------------------------------------------------------------------------
// froxelSliceToViewDepth — exponential distribution
// ---------------------------------------------------------------------------

TEST(VolumetricFog, SliceDepthStaysInsideNearFar)
{
    const FroxelGridConfig g = defaultGrid();
    for (int s = 0; s < g.resZ; ++s)
    {
        const float z = froxelSliceToViewDepth(g, s);
        EXPECT_GE(z, g.near);
        EXPECT_LE(z, g.far);
    }
}

TEST(VolumetricFog, SliceDepthIsStrictlyIncreasing)
{
    const FroxelGridConfig g = defaultGrid();
    float prev = -1.0f;
    for (int s = 0; s < g.resZ; ++s)
    {
        const float z = froxelSliceToViewDepth(g, s);
        EXPECT_GT(z, prev) << "slice " << s;
        prev = z;
    }
}

TEST(VolumetricFog, FirstSliceCentreMatchesClosedForm)
{
    const FroxelGridConfig g = defaultGrid();
    // viewDepth(0) = near * (far/near) ^ (0.5 / N)
    const float expected = g.near
        * std::pow(g.far / g.near, 0.5f / static_cast<float>(g.resZ));
    EXPECT_NEAR(froxelSliceToViewDepth(g, 0), expected, kEps);
}

TEST(VolumetricFog, OutOfRangeSliceIsClamped)
{
    const FroxelGridConfig g = defaultGrid();
    EXPECT_FLOAT_EQ(froxelSliceToViewDepth(g, -10), froxelSliceToViewDepth(g, 0));
    EXPECT_FLOAT_EQ(froxelSliceToViewDepth(g, 9999),
                    froxelSliceToViewDepth(g, g.resZ - 1));
}

TEST(VolumetricFog, DegenerateConfigReturnsNearWithoutNaN)
{
    FroxelGridConfig g = defaultGrid();
    g.near = 0.0f; // invalid — log/pow would blow up
    const float z = froxelSliceToViewDepth(g, 5);
    EXPECT_FALSE(std::isnan(z));
    EXPECT_FALSE(std::isinf(z));

    g = defaultGrid();
    g.far = g.near; // zero span
    EXPECT_FALSE(std::isnan(froxelSliceToViewDepth(g, 5)));
}

// ---------------------------------------------------------------------------
// viewDepthToFroxelSlice — inverse + round-trip
// ---------------------------------------------------------------------------

TEST(VolumetricFog, DepthToSliceRoundTripsSliceToDepth)
{
    const FroxelGridConfig g = defaultGrid();
    for (int s = 0; s < g.resZ; ++s)
    {
        const float z = froxelSliceToViewDepth(g, s);
        const float back = viewDepthToFroxelSlice(g, z);
        EXPECT_NEAR(back, static_cast<float>(s), 1e-3f) << "slice " << s;
    }
}

TEST(VolumetricFog, DepthToSliceKneesAtNearAndFar)
{
    const FroxelGridConfig g = defaultGrid();
    // z == near -> slice -0.5 ; z == far -> slice N-0.5
    EXPECT_NEAR(viewDepthToFroxelSlice(g, g.near), -0.5f, kEps);
    EXPECT_NEAR(viewDepthToFroxelSlice(g, g.far),
                static_cast<float>(g.resZ) - 0.5f, kEps);
}

TEST(VolumetricFog, DepthToSliceClampsOutsideRange)
{
    const FroxelGridConfig g = defaultGrid();
    EXPECT_NEAR(viewDepthToFroxelSlice(g, g.near * 0.1f), -0.5f, kEps);
    EXPECT_NEAR(viewDepthToFroxelSlice(g, g.far * 10.0f),
                static_cast<float>(g.resZ) - 0.5f, kEps);
}

TEST(VolumetricFog, DepthToSliceDegenerateReturnsZero)
{
    FroxelGridConfig g = defaultGrid();
    g.far = g.near;
    EXPECT_FLOAT_EQ(viewDepthToFroxelSlice(g, 50.0f), 0.0f);
}

// ---------------------------------------------------------------------------
// froxelToScreenUV
// ---------------------------------------------------------------------------

TEST(VolumetricFog, ScreenUVCentresTheTile)
{
    const FroxelGridConfig g = defaultGrid();
    const glm::vec2 uv = froxelToScreenUV(g, 0, 0);
    EXPECT_NEAR(uv.x, 0.5f / 160.0f, kEps);
    EXPECT_NEAR(uv.y, 0.5f / 90.0f, kEps);

    const glm::vec2 last = froxelToScreenUV(g, g.resX - 1, g.resY - 1);
    EXPECT_NEAR(last.x, (159.0f + 0.5f) / 160.0f, kEps);
    EXPECT_NEAR(last.y, (89.0f + 0.5f) / 90.0f, kEps);
}

TEST(VolumetricFog, ScreenUVDegenerateResolutionIsZero)
{
    FroxelGridConfig g = defaultGrid();
    g.resX = 0;
    EXPECT_FLOAT_EQ(froxelToScreenUV(g, 3, 3).x, 0.0f);
}

// ---------------------------------------------------------------------------
// froxelSliceBoundaryViewDepth — integer slice boundaries (no centre offset)
// ---------------------------------------------------------------------------

TEST(VolumetricFog, BoundaryZeroIsNearAndResZIsFar)
{
    const FroxelGridConfig g = defaultGrid();
    EXPECT_NEAR(froxelSliceBoundaryViewDepth(g, 0), g.near, kEps);
    EXPECT_NEAR(froxelSliceBoundaryViewDepth(g, g.resZ), g.far, kEps);
}

TEST(VolumetricFog, BoundaryBracketsTheSliceCentre)
{
    // Slice k's centre depth must lie strictly between boundary(k) and
    // boundary(k+1) — that is what makes the centre/boundary pair consistent.
    const FroxelGridConfig g = defaultGrid();
    for (int k = 0; k < g.resZ; ++k)
    {
        const float lo     = froxelSliceBoundaryViewDepth(g, k);
        const float hi     = froxelSliceBoundaryViewDepth(g, k + 1);
        const float centre = froxelSliceToViewDepth(g, k);
        EXPECT_GT(centre, lo) << "slice " << k;
        EXPECT_LT(centre, hi) << "slice " << k;
    }
}

TEST(VolumetricFog, BoundaryIsClampedAndDegenerateSafe)
{
    const FroxelGridConfig g = defaultGrid();
    EXPECT_FLOAT_EQ(froxelSliceBoundaryViewDepth(g, -5), froxelSliceBoundaryViewDepth(g, 0));
    EXPECT_FLOAT_EQ(froxelSliceBoundaryViewDepth(g, g.resZ + 99),
                    froxelSliceBoundaryViewDepth(g, g.resZ));

    FroxelGridConfig bad = defaultGrid();
    bad.near = 0.0f;
    const float z = froxelSliceBoundaryViewDepth(bad, 3);
    EXPECT_FALSE(std::isnan(z));
    EXPECT_FALSE(std::isinf(z));
}

// ---------------------------------------------------------------------------
// henyeyGreensteinPhase — normalised phase function
// ---------------------------------------------------------------------------

TEST(VolumetricFog, IsotropicPhaseIsUniformOneOverFourPi)
{
    constexpr float kOneOverFourPi = 0.0795774715f; // 1 / (4π)
    // g = 0 ⇒ phase is constant 1/(4π) for every angle.
    for (float c : {-1.0f, -0.3f, 0.0f, 0.5f, 1.0f})
    {
        EXPECT_NEAR(henyeyGreensteinPhase(c, 0.0f), kOneOverFourPi, kEps) << "cos=" << c;
    }
}

TEST(VolumetricFog, ForwardScatterPeaksTowardLight)
{
    // Positive g forward-scatters: phase at cosθ = +1 (aligned) must exceed
    // phase at cosθ = -1 (back-scatter).
    const float g = 0.6f;
    EXPECT_GT(henyeyGreensteinPhase(1.0f, g), henyeyGreensteinPhase(-1.0f, g));
}

TEST(VolumetricFog, PhaseIntegratesToUnityOverSphere)
{
    // ∫ p(cosθ) dΩ = 2π ∫_{-1}^{1} p(μ) dμ = 1. Midpoint-integrate in μ.
    const float g = 0.4f;
    constexpr int N = 20000;
    double integral = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const float mu = -1.0f + (static_cast<float>(i) + 0.5f) * (2.0f / N);
        integral += static_cast<double>(henyeyGreensteinPhase(mu, g)) * (2.0 / N);
    }
    integral *= 2.0 * 3.14159265358979323846;
    EXPECT_NEAR(integral, 1.0, 2e-3);
}

TEST(VolumetricFog, PhaseStaysFiniteAtExtremeAnisotropy)
{
    EXPECT_TRUE(std::isfinite(henyeyGreensteinPhase(1.0f, 0.99f)));
    EXPECT_TRUE(std::isfinite(henyeyGreensteinPhase(-1.0f, -0.99f)));
}
