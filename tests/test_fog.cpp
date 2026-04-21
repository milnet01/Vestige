// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_fog.cpp
/// @brief Phase 10 fog primitives — distance fog (Linear / GL_EXP /
///        GL_EXP2), Quílez analytic height fog, sun-inscatter lobe,
///        and the CPU ↔ GPU composite parity surface.

#include <gtest/gtest.h>

#include "renderer/fog.h"

#include <glm/vec3.hpp>

#include <cmath>

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;

FogParams defaultFog()
{
    return FogParams{};  // Defaults: start=20, end=200, density=0.02
}

HeightFogParams defaultHeight()
{
    return HeightFogParams{};  // Defaults: groundDensity=0.05, falloff=0.5
}

SunInscatterParams defaultSun()
{
    return SunInscatterParams{};  // Defaults: exponent=4.0, startDistance=5.0
}
}

// -----------------------------------------------------------------------
// Label stability
// -----------------------------------------------------------------------

TEST(Fog, ModeLabelsAreStable)
{
    EXPECT_STREQ(fogModeLabel(FogMode::None),               "None");
    EXPECT_STREQ(fogModeLabel(FogMode::Linear),             "Linear");
    EXPECT_STREQ(fogModeLabel(FogMode::Exponential),        "Exponential");
    EXPECT_STREQ(fogModeLabel(FogMode::ExponentialSquared), "ExponentialSquared");
}

// -----------------------------------------------------------------------
// Distance fog — None is a pass-through
// -----------------------------------------------------------------------

TEST(Fog, NoneReturnsUnityAtAnyDistance)
{
    FogParams p = defaultFog();
    for (float d : {0.0f, 1.0f, 50.0f, 500.0f, 1e6f})
    {
        EXPECT_NEAR(computeFogFactor(FogMode::None, p, d), 1.0f, kEps)
            << "d=" << d;
    }
}

// -----------------------------------------------------------------------
// Distance fog — all modes: zero / negative distance returns 1.0
// -----------------------------------------------------------------------

TEST(Fog, ZeroOrNegativeDistanceReturnsUnity)
{
    FogParams p = defaultFog();
    for (FogMode m : {FogMode::Linear, FogMode::Exponential,
                      FogMode::ExponentialSquared})
    {
        EXPECT_NEAR(computeFogFactor(m, p, 0.0f),   1.0f, kEps)
            << "mode=" << fogModeLabel(m);
        EXPECT_NEAR(computeFogFactor(m, p, -5.0f),  1.0f, kEps)
            << "mode=" << fogModeLabel(m);
        EXPECT_NEAR(computeFogFactor(m, p, -1e6f),  1.0f, kEps)
            << "mode=" << fogModeLabel(m);
    }
}

// -----------------------------------------------------------------------
// Linear mode knees
// -----------------------------------------------------------------------

TEST(Fog, LinearUnityBelowStart)
{
    FogParams p = defaultFog();  // start=20
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, 1.0f),         1.0f, kEps);
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, p.start),      1.0f, kEps);
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, p.start - 5),  1.0f, kEps);
}

TEST(Fog, LinearZeroAtEnd)
{
    FogParams p = defaultFog();
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, p.end), 0.0f, kEps);
}

TEST(Fog, LinearHalfwayIsHalfFactor)
{
    FogParams p = defaultFog();
    const float mid = (p.start + p.end) * 0.5f;
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, mid), 0.5f, kEps);
}

TEST(Fog, LinearClampsPastEnd)
{
    // Beyond `end` must stay 0 — shader can't reconstruct depth past
    // the far plane, but the math mustn't go negative either.
    FogParams p = defaultFog();
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, 1000.0f), 0.0f, kEps);
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, 1e6f),    0.0f, kEps);
}

TEST(Fog, LinearZeroSpanReturnsUnity)
{
    // start == end is a degenerate user configuration (editor slider
    // at matching values) — return pass-through rather than divide
    // by zero.
    FogParams p;
    p.start = 50.0f;
    p.end   = 50.0f;
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, 50.0f), 1.0f, kEps);
    EXPECT_NEAR(computeFogFactor(FogMode::Linear, p, 100.0f), 1.0f, kEps);
}

// -----------------------------------------------------------------------
// Exponential (GL_EXP) mode
// -----------------------------------------------------------------------

TEST(Fog, ExponentialHalfAtLn2OverDensity)
{
    // factor = exp(-density * d). When density * d = ln(2), factor = 0.5.
    FogParams p = defaultFog();
    p.density = 0.1f;
    const float d = std::log(2.0f) / p.density;  // ≈ 6.931
    EXPECT_NEAR(computeFogFactor(FogMode::Exponential, p, d), 0.5f, kEps);
}

TEST(Fog, ExponentialZeroDensityReturnsUnity)
{
    // density = 0 → exp(0) = 1 (no fog). Degenerate but sensible.
    FogParams p = defaultFog();
    p.density = 0.0f;
    EXPECT_NEAR(computeFogFactor(FogMode::Exponential, p, 100.0f), 1.0f, kEps);
}

TEST(Fog, ExponentialNegativeDensityClampsToZero)
{
    // Defensive: negative density should behave like zero, not produce
    // runaway growth.
    FogParams p = defaultFog();
    p.density = -0.5f;
    EXPECT_NEAR(computeFogFactor(FogMode::Exponential, p, 100.0f), 1.0f, kEps);
}

TEST(Fog, ExponentialDecaysMonotonically)
{
    FogParams p = defaultFog();
    p.density = 0.05f;
    float prev = computeFogFactor(FogMode::Exponential, p, 0.0f);
    for (float d = 1.0f; d <= 200.0f; d += 1.0f)
    {
        float f = computeFogFactor(FogMode::Exponential, p, d);
        EXPECT_LE(f, prev + kEps) << "non-monotonic at d=" << d;
        prev = f;
    }
}

// -----------------------------------------------------------------------
// Exponential-squared (GL_EXP2) mode
// -----------------------------------------------------------------------

TEST(Fog, ExponentialSquaredAtKnee)
{
    // factor = exp(-(density*d)^2). At density*d = 1 → factor = exp(-1).
    FogParams p = defaultFog();
    p.density = 0.01f;
    const float d = 1.0f / p.density;  // density*d = 1
    EXPECT_NEAR(computeFogFactor(FogMode::ExponentialSquared, p, d),
                std::exp(-1.0f), kEps);
}

TEST(Fog, ExponentialSquaredSofterOnsetThanExp)
{
    // Near the camera EXP2 should be *closer to 1* than EXP for the
    // same density (that's the whole point of the squared form).
    FogParams p = defaultFog();
    p.density = 0.05f;
    const float nearD = 5.0f;
    const float fExp  = computeFogFactor(FogMode::Exponential,        p, nearD);
    const float fExp2 = computeFogFactor(FogMode::ExponentialSquared, p, nearD);
    EXPECT_GT(fExp2, fExp);
}

TEST(Fog, ExponentialSquaredDecaysMonotonically)
{
    FogParams p = defaultFog();
    p.density = 0.05f;
    float prev = computeFogFactor(FogMode::ExponentialSquared, p, 0.0f);
    for (float d = 1.0f; d <= 200.0f; d += 1.0f)
    {
        float f = computeFogFactor(FogMode::ExponentialSquared, p, d);
        EXPECT_LE(f, prev + kEps) << "non-monotonic at d=" << d;
        prev = f;
    }
}

// -----------------------------------------------------------------------
// applyFog — CPU ↔ GLSL mix() parity
// -----------------------------------------------------------------------

TEST(Fog, ApplyFogMatchesMixFormula)
{
    // mix(fog, surface, factor) = fog*(1-t) + surface*t.
    const glm::vec3 surface(1.0f, 0.5f, 0.25f);
    const glm::vec3 fog    (0.0f, 0.0f, 1.0f);

    glm::vec3 r0 = applyFog(surface, fog, 1.0f);
    EXPECT_NEAR(r0.r, surface.r, kEps);
    EXPECT_NEAR(r0.g, surface.g, kEps);
    EXPECT_NEAR(r0.b, surface.b, kEps);

    glm::vec3 r1 = applyFog(surface, fog, 0.0f);
    EXPECT_NEAR(r1.r, fog.r, kEps);
    EXPECT_NEAR(r1.g, fog.g, kEps);
    EXPECT_NEAR(r1.b, fog.b, kEps);

    glm::vec3 r2 = applyFog(surface, fog, 0.5f);
    EXPECT_NEAR(r2.r, 0.5f * surface.r + 0.5f * fog.r, kEps);
    EXPECT_NEAR(r2.g, 0.5f * surface.g + 0.5f * fog.g, kEps);
    EXPECT_NEAR(r2.b, 0.5f * surface.b + 0.5f * fog.b, kEps);
}

TEST(Fog, ApplyFogClampsOutOfRangeFactor)
{
    const glm::vec3 surface(1.0f, 0.0f, 0.0f);
    const glm::vec3 fog    (0.0f, 0.0f, 1.0f);

    // factor > 1 should clamp to 1 (pure surface), not produce
    // out-of-bounds colour values.
    glm::vec3 r = applyFog(surface, fog, 1.5f);
    EXPECT_NEAR(r.r, surface.r, kEps);

    // factor < 0 should clamp to 0 (pure fog).
    r = applyFog(surface, fog, -0.5f);
    EXPECT_NEAR(r.b, fog.b, kEps);
}

// -----------------------------------------------------------------------
// Exponential height fog (Quílez)
// -----------------------------------------------------------------------

TEST(Fog, HeightFogZeroLengthRayIsTransparent)
{
    HeightFogParams h = defaultHeight();
    EXPECT_NEAR(computeHeightFogTransmittance(h, 0.0f, 0.0f, 0.0f), 1.0f, kEps);
    EXPECT_NEAR(computeHeightFogTransmittance(h, 10.0f, 1.0f, 0.0f), 1.0f, kEps);
}

TEST(Fog, HeightFogZeroDensityIsTransparent)
{
    HeightFogParams h = defaultHeight();
    h.groundDensity = 0.0f;
    EXPECT_NEAR(computeHeightFogTransmittance(h, 0.0f, 0.5f, 100.0f), 1.0f, kEps);
}

TEST(Fog, HeightFogDecreasesWithDistance)
{
    // Horizontal ray at ground level — transmittance should decay
    // monotonically as we look further through the fog.
    HeightFogParams h = defaultHeight();
    float prev = 1.0f;
    for (float t = 1.0f; t <= 500.0f; t += 10.0f)
    {
        float tr = computeHeightFogTransmittance(h, 0.0f, 0.0f, t);
        EXPECT_LE(tr, prev + kEps) << "non-monotonic at t=" << t;
        prev = tr;
    }
}

TEST(Fog, HeightFogHorizontalRayMatchesBeerLambert)
{
    // With rayDirY == 0 the analytic form collapses to
    //   transmittance = exp(-groundDensity * exp(-b*h) * t)
    // Verify for a ray at fogHeight.
    HeightFogParams h = defaultHeight();
    h.heightFalloff = 0.5f;
    h.groundDensity = 0.05f;
    h.fogHeight     = 0.0f;

    const float cameraY  = 0.0f;  // at fogHeight
    const float t        = 100.0f;
    const float expected = std::exp(-h.groundDensity * t);
    const float actual   = computeHeightFogTransmittance(h, cameraY, 0.0f, t);
    EXPECT_NEAR(actual, expected, 1e-3f);
}

TEST(Fog, HeightFogThinnerAtAltitude)
{
    // Looking horizontally from above the fog layer should leave
    // more transmittance (thinner fog up high).
    HeightFogParams h = defaultHeight();
    const float t = 100.0f;
    const float lowT  = computeHeightFogTransmittance(h, 0.0f,   0.0f, t);  // at ground
    const float highT = computeHeightFogTransmittance(h, 20.0f,  0.0f, t);  // above fog
    EXPECT_GT(highT, lowT);
}

TEST(Fog, HeightFogMaxOpacityCapsAttenuation)
{
    // With maxOpacity 0.8 the floor transmittance is 0.2 — fog can
    // never fully obscure the surface.
    HeightFogParams h = defaultHeight();
    h.groundDensity = 10.0f;  // pea-soup
    h.maxOpacity    = 0.8f;

    const float tr = computeHeightFogTransmittance(h, 0.0f, 0.0f, 1000.0f);
    EXPECT_GE(tr, 0.2f - kEps);
}

TEST(Fog, HeightFogSmallAngleMatchesHorizontalBranch)
{
    // The horizontal-ray branch (|rd.y| < epsilon) and the general
    // branch must agree at small `rd.y` to sub-float tolerance,
    // otherwise the shader will show a visible artefact at the
    // horizon line.
    HeightFogParams h = defaultHeight();
    const float t = 100.0f;

    const float horizontal = computeHeightFogTransmittance(h, 0.0f, 0.0f,    t);
    const float tinyAngle  = computeHeightFogTransmittance(h, 0.0f, 1e-4f,   t);
    EXPECT_NEAR(horizontal, tinyAngle, 1e-3f);
}

// -----------------------------------------------------------------------
// Sun inscatter lobe
// -----------------------------------------------------------------------

TEST(Fog, SunInscatterZeroInsideStartDistance)
{
    SunInscatterParams s = defaultSun();
    const glm::vec3 view(0.0f, 0.0f, -1.0f);
    const glm::vec3 sun(0.0f, 0.0f, 1.0f);  // sun behind camera, light going -z
    // view * -sun = 1.0, cosAngle = 1, but distance < start → zero.
    EXPECT_NEAR(computeSunInscatterLobe(s, view, sun, 1.0f), 0.0f, kEps);
}

TEST(Fog, SunInscatterMaxWhenLookingIntoSun)
{
    SunInscatterParams s = defaultSun();
    // view direction = (0,0,-1) (looking into -z).
    // sunDirection  = (0,0,1)  (light travels in +z direction).
    // -sunDirection = (0,0,-1) ← aligns with view → cos = 1.
    const glm::vec3 view(0.0f, 0.0f, -1.0f);
    const glm::vec3 sunDir(0.0f, 0.0f, 1.0f);
    const float lobe = computeSunInscatterLobe(s, view, sunDir, 100.0f);
    EXPECT_NEAR(lobe, 1.0f, kEps);
}

TEST(Fog, SunInscatterZeroWhenBacklit)
{
    SunInscatterParams s = defaultSun();
    // View away from the sun → cosAngle negative → clamped to 0.
    const glm::vec3 view(0.0f, 0.0f, 1.0f);
    const glm::vec3 sunDir(0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(computeSunInscatterLobe(s, view, sunDir, 100.0f), 0.0f, kEps);
}

TEST(Fog, SunInscatterTighterLobeWithLargerExponent)
{
    SunInscatterParams s = defaultSun();

    // 60° off sun direction — cos = 0.5.
    const glm::vec3 view(std::sin(60.0f * 3.14159265f / 180.0f),
                         0.0f,
                         -std::cos(60.0f * 3.14159265f / 180.0f));
    const glm::vec3 sunDir(0.0f, 0.0f, 1.0f);

    s.exponent = 2.0f;
    const float broad = computeSunInscatterLobe(s, view, sunDir, 100.0f);

    s.exponent = 16.0f;
    const float tight = computeSunInscatterLobe(s, view, sunDir, 100.0f);

    // Larger exponent → lobe falls off faster → lower weight at 60°.
    EXPECT_LT(tight, broad);
}

TEST(Fog, SunInscatterNegativeExponentTreatedAsZero)
{
    SunInscatterParams s = defaultSun();
    s.exponent = -5.0f;
    // pow(cos, 0) = 1 everywhere in the forward hemisphere — defensive
    // handling of bad input.
    const glm::vec3 view(0.0f, 0.0f, -1.0f);
    const glm::vec3 sunDir(0.0f, 0.0f, 1.0f);
    const float lobe = computeSunInscatterLobe(s, view, sunDir, 100.0f);
    EXPECT_NEAR(lobe, 1.0f, kEps);
}
