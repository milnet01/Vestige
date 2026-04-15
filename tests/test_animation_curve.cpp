// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_animation_curve.cpp
/// @brief Unit tests for AnimationCurve and ColorGradient.
#include "editor/widgets/animation_curve.h"
#include "editor/widgets/color_gradient.h"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

using namespace Vestige;

// ---------------------------------------------------------------------------
// AnimationCurve tests
// ---------------------------------------------------------------------------

TEST(AnimationCurveTest, DefaultCurveEvaluatesCorrectly)
{
    AnimationCurve curve;  // default: (0,1) -> (1,0)

    EXPECT_FLOAT_EQ(curve.evaluate(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.5f), 0.5f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.25f), 0.75f);
}

TEST(AnimationCurveTest, EvaluateClampsBelowFirst)
{
    AnimationCurve curve;
    EXPECT_FLOAT_EQ(curve.evaluate(-1.0f), 1.0f);
}

TEST(AnimationCurveTest, EvaluateClampsAboveLast)
{
    AnimationCurve curve;
    EXPECT_FLOAT_EQ(curve.evaluate(2.0f), 0.0f);
}

TEST(AnimationCurveTest, SingleKeyframeReturnsConstant)
{
    AnimationCurve curve;
    curve.keyframes = {{0.5f, 0.7f}};
    EXPECT_FLOAT_EQ(curve.evaluate(0.0f), 0.7f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.5f), 0.7f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 0.7f);
}

TEST(AnimationCurveTest, EmptyCurveReturnsZero)
{
    AnimationCurve curve;
    curve.keyframes.clear();
    EXPECT_FLOAT_EQ(curve.evaluate(0.5f), 0.0f);
}

TEST(AnimationCurveTest, MultipleKeyframes)
{
    AnimationCurve curve;
    curve.keyframes = {{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f}};

    EXPECT_FLOAT_EQ(curve.evaluate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.25f), 0.5f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.5f), 1.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.75f), 0.5f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 0.0f);
}

TEST(AnimationCurveTest, AddKeyframeKeepsSorted)
{
    AnimationCurve curve;
    curve.keyframes = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    curve.addKeyframe(0.5f, 0.5f);

    EXPECT_EQ(curve.keyframes.size(), 3u);
    EXPECT_FLOAT_EQ(curve.keyframes[0].time, 0.0f);
    EXPECT_FLOAT_EQ(curve.keyframes[1].time, 0.5f);
    EXPECT_FLOAT_EQ(curve.keyframes[2].time, 1.0f);
}

TEST(AnimationCurveTest, RemoveKeyframeKeepsMinTwo)
{
    AnimationCurve curve;  // default 2 keyframes
    curve.removeKeyframe(0);
    EXPECT_EQ(curve.keyframes.size(), 2u);  // unchanged

    curve.addKeyframe(0.5f, 0.5f);  // now 3
    curve.removeKeyframe(1);
    EXPECT_EQ(curve.keyframes.size(), 2u);
}

TEST(AnimationCurveTest, JsonRoundTrip)
{
    AnimationCurve curve;
    curve.keyframes = {{0.0f, 0.0f}, {0.3f, 0.8f}, {0.7f, 0.2f}, {1.0f, 1.0f}};

    nlohmann::json j = curve.toJson();
    AnimationCurve restored = AnimationCurve::fromJson(j);

    EXPECT_EQ(restored.keyframes.size(), curve.keyframes.size());
    for (size_t i = 0; i < curve.keyframes.size(); ++i)
    {
        EXPECT_FLOAT_EQ(restored.keyframes[i].time, curve.keyframes[i].time);
        EXPECT_FLOAT_EQ(restored.keyframes[i].value, curve.keyframes[i].value);
    }
}

// ---------------------------------------------------------------------------
// ColorGradient tests
// ---------------------------------------------------------------------------

TEST(ColorGradientTest, DefaultGradientFadesAlpha)
{
    ColorGradient grad;  // default: white opaque -> white transparent

    glm::vec4 start = grad.evaluate(0.0f);
    EXPECT_FLOAT_EQ(start.r, 1.0f);
    EXPECT_FLOAT_EQ(start.a, 1.0f);

    glm::vec4 end = grad.evaluate(1.0f);
    EXPECT_FLOAT_EQ(end.r, 1.0f);
    EXPECT_FLOAT_EQ(end.a, 0.0f);

    glm::vec4 mid = grad.evaluate(0.5f);
    EXPECT_NEAR(mid.a, 0.5f, 0.01f);
}

TEST(ColorGradientTest, EvaluateClampsBelowFirst)
{
    ColorGradient grad;
    glm::vec4 c = grad.evaluate(-1.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(ColorGradientTest, EvaluateClampsAboveLast)
{
    ColorGradient grad;
    glm::vec4 c = grad.evaluate(2.0f);
    EXPECT_FLOAT_EQ(c.a, 0.0f);
}

TEST(ColorGradientTest, SingleStopReturnsConstant)
{
    ColorGradient grad;
    grad.stops = {{0.5f, {1, 0, 0, 1}}};
    glm::vec4 c = grad.evaluate(0.0f);
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
}

TEST(ColorGradientTest, ThreeStopGradient)
{
    ColorGradient grad;
    grad.stops = {
        {0.0f, {1, 0, 0, 1}},   // red
        {0.5f, {0, 1, 0, 1}},   // green
        {1.0f, {0, 0, 1, 1}},   // blue
    };

    glm::vec4 atQuarter = grad.evaluate(0.25f);
    EXPECT_NEAR(atQuarter.r, 0.5f, 0.01f);
    EXPECT_NEAR(atQuarter.g, 0.5f, 0.01f);
    EXPECT_NEAR(atQuarter.b, 0.0f, 0.01f);

    glm::vec4 atMid = grad.evaluate(0.5f);
    EXPECT_FLOAT_EQ(atMid.g, 1.0f);
}

TEST(ColorGradientTest, AddStopKeepsSorted)
{
    ColorGradient grad;
    grad.addStop(0.5f, {0, 1, 0, 1});
    EXPECT_EQ(grad.stops.size(), 3u);
    EXPECT_FLOAT_EQ(grad.stops[1].position, 0.5f);
}

TEST(ColorGradientTest, RemoveStopKeepsMinTwo)
{
    ColorGradient grad;  // 2 stops
    grad.removeStop(0);
    EXPECT_EQ(grad.stops.size(), 2u);

    grad.addStop(0.5f, {0, 1, 0, 1});  // now 3
    grad.removeStop(1);
    EXPECT_EQ(grad.stops.size(), 2u);
}

TEST(ColorGradientTest, JsonRoundTrip)
{
    ColorGradient grad;
    grad.stops = {
        {0.0f, {1.0f, 0.5f, 0.0f, 1.0f}},
        {0.5f, {0.0f, 1.0f, 0.5f, 0.8f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 0.0f}},
    };

    nlohmann::json j = grad.toJson();
    ColorGradient restored = ColorGradient::fromJson(j);

    EXPECT_EQ(restored.stops.size(), grad.stops.size());
    for (size_t i = 0; i < grad.stops.size(); ++i)
    {
        EXPECT_FLOAT_EQ(restored.stops[i].position, grad.stops[i].position);
        EXPECT_FLOAT_EQ(restored.stops[i].color.r, grad.stops[i].color.r);
        EXPECT_FLOAT_EQ(restored.stops[i].color.g, grad.stops[i].color.g);
        EXPECT_FLOAT_EQ(restored.stops[i].color.b, grad.stops[i].color.b);
        EXPECT_FLOAT_EQ(restored.stops[i].color.a, grad.stops[i].color.a);
    }
}
