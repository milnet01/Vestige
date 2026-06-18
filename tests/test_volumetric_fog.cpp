// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_volumetric_fog.cpp
/// @brief Phase 10 slice 11.6 — froxel-grid coordinate math: exponential
///        depth-slice distribution, its inverse, and screen-UV tiling.
///        These pin the GPU compute shaders (CLAUDE.md Rule 7).

#include <gtest/gtest.h>

#include "renderer/volumetric_fog.h"

#include <algorithm>
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

// ---------------------------------------------------------------------------
// fogDensityNoise (slice 11.8) — CPU spec; pinned to the GLSL field by the
// GPU parity test (GlslDensityNoiseMatchesCpuReference).
// ---------------------------------------------------------------------------

namespace
{
FogNoiseParams noiseParams(float strength, int octaves = 3)
{
    FogNoiseParams p;
    p.frequency    = 0.05f;
    p.strength     = strength;
    p.octaves      = octaves;
    p.windVelocity = glm::vec3(0.4f, 0.1f, 0.2f);
    return p;
}
} // namespace

TEST(VolumetricFogNoise, MultiplierStaysInStrengthBand)
{
    const FogNoiseParams p = noiseParams(0.6f);
    for (int i = 0; i < 50; ++i)
    {
        const glm::vec3 wp(static_cast<float>(i) * 3.1f - 70.0f,
                           static_cast<float>(i) * -1.7f,
                           static_cast<float>(i) * 2.3f);
        const float m = fogDensityNoise(wp, p, 0.0f);
        EXPECT_GE(m, 1.0f - p.strength - kEps);
        EXPECT_LE(m, 1.0f + p.strength + kEps);
        EXPECT_GE(m, 0.0f);
        EXPECT_LE(m, 2.0f);
    }
}

TEST(VolumetricFogNoise, Deterministic)
{
    const FogNoiseParams p = noiseParams(0.6f);
    const glm::vec3 wp(4.2f, -1.1f, 9.9f);
    EXPECT_FLOAT_EQ(fogDensityNoise(wp, p, 3.0f), fogDensityNoise(wp, p, 3.0f));
}

TEST(VolumetricFogNoise, StrengthZeroIsUnity)
{
    const FogNoiseParams p = noiseParams(0.0f);
    for (int i = 0; i < 10; ++i)
    {
        const glm::vec3 wp(static_cast<float>(i),
                           -static_cast<float>(i),
                           2.0f * static_cast<float>(i));
        EXPECT_FLOAT_EQ(fogDensityNoise(wp, p, 1.0f), 1.0f);
    }
}

TEST(VolumetricFogNoise, WindScrollAnimatesField)
{
    const FogNoiseParams p = noiseParams(0.6f);
    const glm::vec3 wp(2.0f, 1.0f, 3.0f);
    // Same point, different times → the field has drifted (non-zero wind).
    EXPECT_NE(fogDensityNoise(wp, p, 0.0f), fogDensityNoise(wp, p, 5.0f));
}

TEST(VolumetricFogNoise, FieldVariesInSpace)
{
    // strength = 1 ⇒ m = 2n, so m straddling 1.0 proves the FBM value n
    // spans both sides of 0.5 — i.e. the field is non-constant, in [0,1].
    const FogNoiseParams p = noiseParams(1.0f);
    float lo = 2.0f, hi = 0.0f;
    for (int i = 0; i < 64; ++i)
    {
        const glm::vec3 wp(static_cast<float>(i) * 5.0f, 0.0f, 0.0f);
        const float m = fogDensityNoise(wp, p, 0.0f);
        EXPECT_GE(m, 0.0f);
        EXPECT_LE(m, 2.0f);
        lo = std::min(lo, m);
        hi = std::max(hi, m);
    }
    EXPECT_LT(lo, 1.0f);
    EXPECT_GT(hi, 1.0f);
}

TEST(VolumetricFogNoise, OctaveCountClampedNoCrash)
{
    const glm::vec3 wp(1.0f, 2.0f, 3.0f);
    for (int oct : {-2, 0, 1, 3, 5, 99})
    {
        const FogNoiseParams p = noiseParams(0.6f, oct);
        const float m = fogDensityNoise(wp, p, 0.0f);
        EXPECT_TRUE(std::isfinite(m));
        EXPECT_GE(m, 0.0f);
        EXPECT_LE(m, 2.0f);
    }
}

// ---------------------------------------------------------------------------
// fogVolumeDensity (slice 11.11) — CPU spec; pinned to the GLSL falloff by the
// GPU parity test (GlslFogVolumeDensityMatchesCpuReference).
// ---------------------------------------------------------------------------

namespace
{
FogVolume boxVolume(float edgeSoftness = 0.2f, float animSpeed = 0.0f)
{
    FogVolume v;
    v.shape        = FogVolumeShape::Box;
    v.center       = glm::vec3(0.0f);
    v.halfExtents  = glm::vec3(2.0f, 3.0f, 4.0f);
    v.edgeSoftness = edgeSoftness;
    v.animSpeed    = animSpeed;
    return v;
}

FogVolume sphereVolume(float edgeSoftness = 0.2f, float animSpeed = 0.0f)
{
    FogVolume v;
    v.shape        = FogVolumeShape::Sphere;
    v.center       = glm::vec3(1.0f, -2.0f, 0.5f);
    v.halfExtents  = glm::vec3(3.0f, 0.0f, 0.0f); // .x = radius
    v.edgeSoftness = edgeSoftness;
    v.animSpeed    = animSpeed;
    return v;
}
} // namespace

TEST(VolumetricFogVolume, CoreIsFullDensity)
{
    EXPECT_FLOAT_EQ(fogVolumeDensity(boxVolume(), glm::vec3(0.0f), 0.0f), 1.0f);
    const FogVolume s = sphereVolume();
    EXPECT_FLOAT_EQ(fogVolumeDensity(s, s.center, 0.0f), 1.0f);
}

TEST(VolumetricFogVolume, OutsideIsZero)
{
    // Far past the outer extent on any axis → no contribution.
    EXPECT_FLOAT_EQ(fogVolumeDensity(boxVolume(), glm::vec3(100.0f, 0.0f, 0.0f), 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(fogVolumeDensity(boxVolume(), glm::vec3(0.0f, 0.0f, 50.0f), 0.0f), 0.0f);
    const FogVolume s = sphereVolume();
    EXPECT_FLOAT_EQ(fogVolumeDensity(s, s.center + glm::vec3(10.0f, 0.0f, 0.0f), 0.0f), 0.0f);
}

TEST(VolumetricFogVolume, EdgeFalloffMonotonicNonIncreasing)
{
    // Box: marching out along +x from core to past the extent never increases.
    const FogVolume v = boxVolume();
    float prev = 2.0f;
    for (int i = 0; i <= 40; ++i)
    {
        const float x = static_cast<float>(i) * 0.1f; // 0 .. 4 m
        const float d = fogVolumeDensity(v, glm::vec3(x, 0.0f, 0.0f), 0.0f);
        EXPECT_GE(d, 0.0f);
        EXPECT_LE(d, 1.0f);
        EXPECT_LE(d, prev + kEps) << "non-monotonic @ x=" << x;
        prev = d;
    }
}

TEST(VolumetricFogVolume, HardEdgeWhenSoftnessZero)
{
    // edgeSoftness == 0 ⇒ a hard step: full inside the extent, zero outside.
    const FogVolume v = boxVolume(0.0f);
    EXPECT_FLOAT_EQ(fogVolumeDensity(v, glm::vec3(1.9f, 2.9f, 3.9f), 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(fogVolumeDensity(v, glm::vec3(2.1f, 0.0f, 0.0f), 0.0f), 0.0f);

    const FogVolume s = sphereVolume(0.0f); // radius 3
    EXPECT_FLOAT_EQ(fogVolumeDensity(s, s.center + glm::vec3(2.9f, 0.0f, 0.0f), 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(fogVolumeDensity(s, s.center + glm::vec3(3.1f, 0.0f, 0.0f), 0.0f), 0.0f);
}

TEST(VolumetricFogVolume, SphereIsRadiallySymmetric)
{
    const FogVolume s = sphereVolume(); // radius 3, edgeSoftness 0.2 → soft band (2.4, 3.0)
    const float r = 2.7f; // within the soft band → density strictly in (0,1)
    const float ax = fogVolumeDensity(s, s.center + glm::vec3(r, 0.0f, 0.0f), 0.0f);
    const float ay = fogVolumeDensity(s, s.center + glm::vec3(0.0f, r, 0.0f), 0.0f);
    const float az = fogVolumeDensity(s, s.center + glm::vec3(0.0f, 0.0f, r), 0.0f);
    EXPECT_NEAR(ax, ay, kEps);
    EXPECT_NEAR(ax, az, kEps);
    EXPECT_GT(ax, 0.0f);
    EXPECT_LT(ax, 1.0f);
}

TEST(VolumetricFogVolume, StaticIsTimeInvariantAnimatedIsNot)
{
    const glm::vec3 wp(0.5f, 0.5f, 0.5f);

    // animSpeed == 0 → no turbulence term → identical across time.
    const FogVolume stat = boxVolume(0.2f, 0.0f);
    EXPECT_FLOAT_EQ(fogVolumeDensity(stat, wp, 0.0f), fogVolumeDensity(stat, wp, 9.0f));

    // animSpeed != 0 → turbulence scroll → the value drifts with time.
    const FogVolume anim = boxVolume(0.2f, 1.0f);
    EXPECT_NE(fogVolumeDensity(anim, wp, 0.0f), fogVolumeDensity(anim, wp, 3.0f));
}

TEST(VolumetricFogVolume, AlwaysInUnitInterval)
{
    const FogVolume anim = boxVolume(0.5f, 2.0f);
    for (int i = 0; i < 64; ++i)
    {
        const glm::vec3 wp(static_cast<float>(i) * 0.13f - 4.0f,
                           static_cast<float>(i) * -0.21f + 3.0f,
                           static_cast<float>(i) * 0.07f);
        const float t = static_cast<float>(i) * 0.5f;
        const float d = fogVolumeDensity(anim, wp, t);
        EXPECT_TRUE(std::isfinite(d));
        EXPECT_GE(d, 0.0f);
        EXPECT_LE(d, 1.0f);
    }
}
