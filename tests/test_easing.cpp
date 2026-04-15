// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_easing.cpp
/// @brief Unit tests for easing functions and cubic bezier curves.
#include "animation/easing.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Boundary tests: all standard easings must return 0 at t=0 and 1 at t=1
// ---------------------------------------------------------------------------

TEST(EasingTest, AllFunctionsReturnZeroAtStart)
{
    for (int i = 0; i < static_cast<int>(EaseType::COUNT); ++i)
    {
        auto type = static_cast<EaseType>(i);
        // Step is special: returns 0 at t=0 (correct), skip the exact check
        if (type == EaseType::STEP) continue;

        float val = evaluateEasing(type, 0.0f);
        EXPECT_NEAR(val, 0.0f, 0.001f) << "Failed for " << easeTypeName(type);
    }
}

TEST(EasingTest, AllFunctionsReturnOneAtEnd)
{
    for (int i = 0; i < static_cast<int>(EaseType::COUNT); ++i)
    {
        auto type = static_cast<EaseType>(i);
        float val = evaluateEasing(type, 1.0f);
        EXPECT_NEAR(val, 1.0f, 0.001f) << "Failed for " << easeTypeName(type);
    }
}

// ---------------------------------------------------------------------------
// Linear and Step
// ---------------------------------------------------------------------------

TEST(EasingTest, LinearIsIdentity)
{
    EXPECT_NEAR(evaluateEasing(EaseType::LINEAR, 0.0f), 0.0f, 0.001f);
    EXPECT_NEAR(evaluateEasing(EaseType::LINEAR, 0.25f), 0.25f, 0.001f);
    EXPECT_NEAR(evaluateEasing(EaseType::LINEAR, 0.5f), 0.5f, 0.001f);
    EXPECT_NEAR(evaluateEasing(EaseType::LINEAR, 0.75f), 0.75f, 0.001f);
    EXPECT_NEAR(evaluateEasing(EaseType::LINEAR, 1.0f), 1.0f, 0.001f);
}

TEST(EasingTest, StepIsZeroThenOne)
{
    EXPECT_NEAR(evaluateEasing(EaseType::STEP, 0.0f), 0.0f, 0.001f);
    EXPECT_NEAR(evaluateEasing(EaseType::STEP, 0.5f), 0.0f, 0.001f);
    EXPECT_NEAR(evaluateEasing(EaseType::STEP, 0.99f), 0.0f, 0.001f);
    EXPECT_NEAR(evaluateEasing(EaseType::STEP, 1.0f), 1.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// EaseIn curves start slow: f(0.5) < 0.5
// ---------------------------------------------------------------------------

TEST(EasingTest, EaseInStartsSlow)
{
    EaseType easeIns[] = {
        EaseType::EASE_IN_QUAD,
        EaseType::EASE_IN_CUBIC,
        EaseType::EASE_IN_QUART,
        EaseType::EASE_IN_QUINT,
        EaseType::EASE_IN_SINE,
        EaseType::EASE_IN_EXPO,
        EaseType::EASE_IN_CIRC,
    };

    for (auto type : easeIns)
    {
        float val = evaluateEasing(type, 0.5f);
        EXPECT_LT(val, 0.5f) << "Failed for " << easeTypeName(type);
    }
}

// ---------------------------------------------------------------------------
// EaseOut curves start fast: f(0.5) > 0.5
// ---------------------------------------------------------------------------

TEST(EasingTest, EaseOutStartsFast)
{
    EaseType easeOuts[] = {
        EaseType::EASE_OUT_QUAD,
        EaseType::EASE_OUT_CUBIC,
        EaseType::EASE_OUT_QUART,
        EaseType::EASE_OUT_QUINT,
        EaseType::EASE_OUT_SINE,
        EaseType::EASE_OUT_EXPO,
        EaseType::EASE_OUT_CIRC,
    };

    for (auto type : easeOuts)
    {
        float val = evaluateEasing(type, 0.5f);
        EXPECT_GT(val, 0.5f) << "Failed for " << easeTypeName(type);
    }
}

// ---------------------------------------------------------------------------
// Specific known values
// ---------------------------------------------------------------------------

TEST(EasingTest, QuadMidpointValues)
{
    // EaseInQuad(0.5) = 0.25
    EXPECT_NEAR(evaluateEasing(EaseType::EASE_IN_QUAD, 0.5f), 0.25f, 0.001f);
    // EaseOutQuad(0.5) = 0.75
    EXPECT_NEAR(evaluateEasing(EaseType::EASE_OUT_QUAD, 0.5f), 0.75f, 0.001f);
    // EaseInOutQuad(0.5) = 0.5 (symmetry point)
    EXPECT_NEAR(evaluateEasing(EaseType::EASE_IN_OUT_QUAD, 0.5f), 0.5f, 0.001f);
}

TEST(EasingTest, CubicMidpointValues)
{
    // EaseInCubic(0.5) = 0.125
    EXPECT_NEAR(evaluateEasing(EaseType::EASE_IN_CUBIC, 0.5f), 0.125f, 0.001f);
    // EaseOutCubic(0.5) = 0.875
    EXPECT_NEAR(evaluateEasing(EaseType::EASE_OUT_CUBIC, 0.5f), 0.875f, 0.001f);
}

TEST(EasingTest, SineMidpointValues)
{
    // EaseInSine(0.5) = 1 - cos(PI/4) ≈ 1 - 0.7071 ≈ 0.2929
    EXPECT_NEAR(evaluateEasing(EaseType::EASE_IN_SINE, 0.5f), 0.2929f, 0.001f);
    // EaseOutSine(0.5) = sin(PI/4) ≈ 0.7071
    EXPECT_NEAR(evaluateEasing(EaseType::EASE_OUT_SINE, 0.5f), 0.7071f, 0.001f);
}

// ---------------------------------------------------------------------------
// Bounce stays in [0,1] range
// ---------------------------------------------------------------------------

TEST(EasingTest, BounceStaysInRange)
{
    for (float t = 0.0f; t <= 1.0f; t += 0.01f)
    {
        float val = evaluateEasing(EaseType::EASE_OUT_BOUNCE, t);
        EXPECT_GE(val, -0.001f) << "Bounce went below 0 at t=" << t;
        EXPECT_LE(val, 1.001f) << "Bounce went above 1 at t=" << t;
    }
}

// ---------------------------------------------------------------------------
// Elastic overshoots (goes outside [0,1])
// ---------------------------------------------------------------------------

TEST(EasingTest, ElasticOvershoots)
{
    bool foundOvershoot = false;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f)
    {
        float val = evaluateEasing(EaseType::EASE_OUT_ELASTIC, t);
        if (val > 1.001f || val < -0.001f)
        {
            foundOvershoot = true;
            break;
        }
    }
    EXPECT_TRUE(foundOvershoot) << "Elastic easing should overshoot";
}

// ---------------------------------------------------------------------------
// Back overshoots
// ---------------------------------------------------------------------------

TEST(EasingTest, BackOvershoots)
{
    // EaseInBack should go negative near the start
    float val = evaluateEasing(EaseType::EASE_IN_BACK, 0.1f);
    EXPECT_LT(val, 0.0f) << "EaseInBack should undershoot at t=0.1";
}

// ---------------------------------------------------------------------------
// Name table
// ---------------------------------------------------------------------------

TEST(EasingTest, NameTableCoversAllTypes)
{
    for (int i = 0; i < static_cast<int>(EaseType::COUNT); ++i)
    {
        auto type = static_cast<EaseType>(i);
        const char* name = easeTypeName(type);
        EXPECT_NE(name, nullptr);
        EXPECT_STRNE(name, "Unknown") << "Missing name for index " << i;
    }
}

TEST(EasingTest, InvalidTypeReturnsUnknown)
{
    EXPECT_STREQ(easeTypeName(static_cast<EaseType>(255)), "Unknown");
}

// ---------------------------------------------------------------------------
// CubicBezierEase
// ---------------------------------------------------------------------------

TEST(CubicBezierTest, LinearEquivalent)
{
    // cubic-bezier(0.25, 0.25, 0.75, 0.75) ≈ linear
    CubicBezierEase linear(0.25f, 0.25f, 0.75f, 0.75f);
    for (float t = 0.0f; t <= 1.0f; t += 0.1f)
    {
        EXPECT_NEAR(linear.evaluate(t), t, 0.02f) << "t=" << t;
    }
}

TEST(CubicBezierTest, BoundaryValues)
{
    CubicBezierEase ease(0.42f, 0.0f, 0.58f, 1.0f);
    EXPECT_NEAR(ease.evaluate(0.0f), 0.0f, 0.001f);
    EXPECT_NEAR(ease.evaluate(1.0f), 1.0f, 0.001f);
}

TEST(CubicBezierTest, EaseInShape)
{
    // CSS ease-in: cubic-bezier(0.42, 0, 1, 1)
    CubicBezierEase easeIn(0.42f, 0.0f, 1.0f, 1.0f);
    // At midpoint, easeIn should produce less than 0.5
    EXPECT_LT(easeIn.evaluate(0.5f), 0.5f);
}

TEST(CubicBezierTest, EaseOutShape)
{
    // CSS ease-out: cubic-bezier(0, 0, 0.58, 1)
    CubicBezierEase easeOut(0.0f, 0.0f, 0.58f, 1.0f);
    // At midpoint, easeOut should produce more than 0.5
    EXPECT_GT(easeOut.evaluate(0.5f), 0.5f);
}
