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

// -----------------------------------------------------------------------
// Full composite — CPU spec for the GLSL screen_quad.frag.glsl path
// -----------------------------------------------------------------------
//
// These tests pin the composition order that the GPU shader implements.
// If the shader drifts from this spec, screenshots will disagree and
// the renderer integration is broken — so either the shader or these
// tests must follow, never in silence.

TEST(FogComposite, AllDisabledIsIdentity)
{
    // No layer active → surface passes through unchanged.
    FogCompositeInputs inputs;
    const glm::vec3 surface(0.3f, 0.6f, 0.9f);
    const glm::vec3 worldPos(10.0f, 0.0f, 0.0f);
    const glm::vec3 out = composeFog(surface, inputs, worldPos);
    EXPECT_NEAR(out.r, surface.r, kEps);
    EXPECT_NEAR(out.g, surface.g, kEps);
    EXPECT_NEAR(out.b, surface.b, kEps);
}

TEST(FogComposite, DistanceFogAtFarEndGivesFogColour)
{
    // Linear mode at end distance → factor 0 → pure fog colour.
    FogCompositeInputs inputs;
    inputs.fogMode = FogMode::Linear;
    inputs.fogParams.start  = 10.0f;
    inputs.fogParams.end    = 100.0f;
    inputs.fogParams.colour = glm::vec3(0.1f, 0.2f, 0.8f);

    const glm::vec3 surface(1.0f, 1.0f, 1.0f);
    const glm::vec3 worldPos(0.0f, 0.0f, -100.0f);  // 100m away from camera at origin
    const glm::vec3 out = composeFog(surface, inputs, worldPos);

    EXPECT_NEAR(out.r, inputs.fogParams.colour.r, kEps);
    EXPECT_NEAR(out.g, inputs.fogParams.colour.g, kEps);
    EXPECT_NEAR(out.b, inputs.fogParams.colour.b, kEps);
}

TEST(FogComposite, DistanceFogNearCameraIsSurface)
{
    // Below start distance → factor 1 → pure surface (no fog yet).
    FogCompositeInputs inputs;
    inputs.fogMode = FogMode::Linear;
    inputs.fogParams.start = 10.0f;
    inputs.fogParams.end   = 100.0f;

    const glm::vec3 surface(0.7f, 0.3f, 0.1f);
    const glm::vec3 worldPos(0.0f, 0.0f, -5.0f);  // Inside start
    const glm::vec3 out = composeFog(surface, inputs, worldPos);

    EXPECT_NEAR(out.r, surface.r, kEps);
    EXPECT_NEAR(out.g, surface.g, kEps);
    EXPECT_NEAR(out.b, surface.b, kEps);
}

TEST(FogComposite, SunInscatterWarmsDistanceFogColour)
{
    // Looking directly into the sun at full distance, the distance-fog
    // colour should be replaced by the sun-inscatter colour (lobe=1).
    FogCompositeInputs inputs;
    inputs.fogMode = FogMode::Linear;
    inputs.fogParams.start  = 5.0f;
    inputs.fogParams.end    = 50.0f;
    inputs.fogParams.colour = glm::vec3(0.5f, 0.5f, 0.5f);

    inputs.sunInscatterEnabled       = true;
    inputs.sunInscatterParams.colour = glm::vec3(1.0f, 0.5f, 0.1f);
    inputs.sunInscatterParams.startDistance = 1.0f;
    // Sun directly behind the surface — view ray (cam→surface) aligns
    // with -sunDirection, so the lobe is maxed at 1.0.
    inputs.sunDirection      = glm::vec3(0.0f, 0.0f, 1.0f);
    inputs.cameraWorldPos    = glm::vec3(0.0f);

    const glm::vec3 surface(1.0f, 1.0f, 1.0f);
    const glm::vec3 worldPos(0.0f, 0.0f, -50.0f);  // At end → factor 0 → pure fog colour
    const glm::vec3 out = composeFog(surface, inputs, worldPos);

    EXPECT_NEAR(out.r, inputs.sunInscatterParams.colour.r, kEps);
    EXPECT_NEAR(out.g, inputs.sunInscatterParams.colour.g, kEps);
    EXPECT_NEAR(out.b, inputs.sunInscatterParams.colour.b, kEps);
}

TEST(FogComposite, HeightFogAppliedAfterDistance)
{
    // Surface fully visible through distance fog (None mode) but
    // height fog dense enough to fully obscure → pure height-fog
    // colour survives.
    FogCompositeInputs inputs;
    inputs.heightFogEnabled = true;
    inputs.heightFogParams.colour        = glm::vec3(0.9f, 0.1f, 0.1f);
    inputs.heightFogParams.groundDensity = 100.0f;  // Opaque
    inputs.heightFogParams.heightFalloff = 0.0f;
    inputs.heightFogParams.maxOpacity    = 1.0f;
    inputs.heightFogParams.fogHeight     = 0.0f;
    inputs.cameraWorldPos = glm::vec3(0.0f, 0.0f, 0.0f);

    const glm::vec3 surface(0.0f, 0.5f, 0.8f);
    const glm::vec3 worldPos(0.0f, 0.0f, -50.0f);  // Horizontal ray
    const glm::vec3 out = composeFog(surface, inputs, worldPos);

    EXPECT_NEAR(out.r, inputs.heightFogParams.colour.r, 1e-3f);
    EXPECT_NEAR(out.g, inputs.heightFogParams.colour.g, 1e-3f);
    EXPECT_NEAR(out.b, inputs.heightFogParams.colour.b, 1e-3f);
}

TEST(FogComposite, OrderDistanceThenHeight)
{
    // Both layers active with 50/50 weight. The composition order
    // matters: distance fog mixes first, then height fog mixes the
    // result with its colour. The test pins the exact algebra that
    // the GLSL shader performs.
    FogCompositeInputs inputs;
    inputs.fogMode = FogMode::Linear;
    inputs.fogParams.start  = 0.0f;
    inputs.fogParams.end    = 100.0f;
    inputs.fogParams.colour = glm::vec3(1.0f, 0.0f, 0.0f);  // Red distance fog

    // Height fog at ground-level ray with controlled density so
    // transmittance lands at 0.5 exactly.
    // exp(-d * t) = 0.5 → d*t = ln(2). With t = 50m, d = ln(2)/50.
    inputs.heightFogEnabled = true;
    inputs.heightFogParams.colour        = glm::vec3(0.0f, 0.0f, 1.0f);  // Blue height fog
    inputs.heightFogParams.groundDensity = std::log(2.0f) / 50.0f;
    inputs.heightFogParams.heightFalloff = 0.0f;  // Beer-Lambert branch
    inputs.heightFogParams.maxOpacity    = 1.0f;
    inputs.heightFogParams.fogHeight     = 0.0f;
    inputs.cameraWorldPos = glm::vec3(0.0f);

    const glm::vec3 surface(0.0f, 1.0f, 0.0f);  // Green surface
    const glm::vec3 worldPos(0.0f, 0.0f, -50.0f);  // 50m horizontal
    // Distance visibility at 50m on [0,100] linear span = 0.5 exactly.
    // Height transmittance at 50m = 0.5 exactly (constructed above).
    // Expected:
    //   fogged = mix(red, green, 0.5) = (0.5, 0.5, 0)
    //   out    = mix(blue, fogged, 0.5) = (0.25, 0.25, 0.5)
    const glm::vec3 out = composeFog(surface, inputs, worldPos);

    EXPECT_NEAR(out.r, 0.25f, 1e-3f);
    EXPECT_NEAR(out.g, 0.25f, 1e-3f);
    EXPECT_NEAR(out.b, 0.5f,  1e-3f);
}

TEST(FogComposite, CameraAtSurfaceIsIdentityEvenWithFogActive)
{
    // Zero view distance → distance fog is pass-through (factor=1)
    // and height fog transmittance is 1 (zero-length-ray guard).
    FogCompositeInputs inputs;
    inputs.fogMode = FogMode::Exponential;
    inputs.fogParams.density = 0.1f;
    inputs.heightFogEnabled  = true;
    inputs.heightFogParams.groundDensity = 0.5f;

    const glm::vec3 surface(0.2f, 0.4f, 0.6f);
    const glm::vec3 worldPos(0.0f, 0.0f, 0.0f);  // Same as camera
    const glm::vec3 out = composeFog(surface, inputs, worldPos);

    EXPECT_NEAR(out.r, surface.r, kEps);
    EXPECT_NEAR(out.g, surface.g, kEps);
    EXPECT_NEAR(out.b, surface.b, kEps);
}
