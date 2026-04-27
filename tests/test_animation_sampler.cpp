// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_animation_sampler.cpp
/// @brief Unit tests for animation sampling (interpolation).
#include "animation/animation_sampler.h"
#include "animation/animation_clip.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helper to build a simple vec3 channel (2 keyframes)
// ---------------------------------------------------------------------------
static AnimationChannel makeVec3Channel(float t0, float t1,
                                         glm::vec3 v0, glm::vec3 v1,
                                         AnimInterpolation interp = AnimInterpolation::LINEAR)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = interp;
    ch.timestamps = {t0, t1};
    ch.values = {v0.x, v0.y, v0.z, v1.x, v1.y, v1.z};
    return ch;
}

// ---------------------------------------------------------------------------
// Vec3 LINEAR tests
// ---------------------------------------------------------------------------

TEST(AnimSamplerTest, LinearVec3AtStart)
{
    auto ch = makeVec3Channel(0.0f, 1.0f, {0, 0, 0}, {10, 20, 30});
    glm::vec3 result = sampleVec3(ch, 0.0f);
    EXPECT_NEAR(result.x, 0.0f, 0.001f);
    EXPECT_NEAR(result.y, 0.0f, 0.001f);
    EXPECT_NEAR(result.z, 0.0f, 0.001f);
}

TEST(AnimSamplerTest, LinearVec3AtEnd)
{
    auto ch = makeVec3Channel(0.0f, 1.0f, {0, 0, 0}, {10, 20, 30});
    glm::vec3 result = sampleVec3(ch, 1.0f);
    EXPECT_NEAR(result.x, 10.0f, 0.001f);
    EXPECT_NEAR(result.y, 20.0f, 0.001f);
    EXPECT_NEAR(result.z, 30.0f, 0.001f);
}

TEST(AnimSamplerTest, LinearVec3AtMidpoint)
{
    auto ch = makeVec3Channel(0.0f, 1.0f, {0, 0, 0}, {10, 20, 30});
    glm::vec3 result = sampleVec3(ch, 0.5f);
    EXPECT_NEAR(result.x, 5.0f, 0.001f);
    EXPECT_NEAR(result.y, 10.0f, 0.001f);
    EXPECT_NEAR(result.z, 15.0f, 0.001f);
}

TEST(AnimSamplerTest, LinearVec3BeforeStart)
{
    auto ch = makeVec3Channel(1.0f, 2.0f, {10, 10, 10}, {20, 20, 20});
    glm::vec3 result = sampleVec3(ch, 0.0f);
    // Should clamp to first keyframe
    EXPECT_NEAR(result.x, 10.0f, 0.001f);
}

TEST(AnimSamplerTest, LinearVec3AfterEnd)
{
    auto ch = makeVec3Channel(0.0f, 1.0f, {0, 0, 0}, {10, 10, 10});
    glm::vec3 result = sampleVec3(ch, 5.0f);
    // Should clamp to last keyframe
    EXPECT_NEAR(result.x, 10.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Vec3 STEP tests
// ---------------------------------------------------------------------------

TEST(AnimSamplerTest, StepVec3AtMidpoint)
{
    auto ch = makeVec3Channel(0.0f, 1.0f, {0, 0, 0}, {10, 20, 30}, AnimInterpolation::STEP);
    glm::vec3 result = sampleVec3(ch, 0.5f);
    // STEP: should return first keyframe value (not interpolated)
    EXPECT_NEAR(result.x, 0.0f, 0.001f);
    EXPECT_NEAR(result.y, 0.0f, 0.001f);
    EXPECT_NEAR(result.z, 0.0f, 0.001f);
}

TEST(AnimSamplerTest, StepVec3AtEnd)
{
    auto ch = makeVec3Channel(0.0f, 1.0f, {0, 0, 0}, {10, 20, 30}, AnimInterpolation::STEP);
    glm::vec3 result = sampleVec3(ch, 1.0f);
    // At or past the last keyframe
    EXPECT_NEAR(result.x, 10.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Quat LINEAR (SLERP) tests
// ---------------------------------------------------------------------------

TEST(AnimSamplerTest, LinearQuatIdentityToRotation)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::ROTATION;
    ch.interpolation = AnimInterpolation::LINEAR;

    // Identity quaternion (x,y,z,w) in glTF order
    glm::quat q0(1.0f, 0.0f, 0.0f, 0.0f);  // w,x,y,z = identity
    // 90 degrees around Y axis
    glm::quat q1 = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));

    ch.timestamps = {0.0f, 1.0f};
    // glTF stores as x,y,z,w
    ch.values = {q0.x, q0.y, q0.z, q0.w,
                 q1.x, q1.y, q1.z, q1.w};

    glm::quat result = sampleQuat(ch, 0.0f);
    // Should be approximately identity
    EXPECT_NEAR(result.w, 1.0f, 0.01f);
    EXPECT_NEAR(result.x, 0.0f, 0.01f);
    EXPECT_NEAR(result.y, 0.0f, 0.01f);
    EXPECT_NEAR(result.z, 0.0f, 0.01f);
}

TEST(AnimSamplerTest, LinearQuatMidpoint)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::ROTATION;
    ch.interpolation = AnimInterpolation::LINEAR;

    glm::quat q0(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat q1 = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));

    ch.timestamps = {0.0f, 1.0f};
    ch.values = {q0.x, q0.y, q0.z, q0.w,
                 q1.x, q1.y, q1.z, q1.w};

    glm::quat result = sampleQuat(ch, 0.5f);
    // Should be ~45 degrees around Y (SLERP midpoint)
    glm::quat expected = glm::slerp(q0, q1, 0.5f);
    EXPECT_NEAR(result.w, expected.w, 0.01f);
    EXPECT_NEAR(result.x, expected.x, 0.01f);
    EXPECT_NEAR(result.y, expected.y, 0.01f);
    EXPECT_NEAR(result.z, expected.z, 0.01f);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(AnimSamplerTest, EmptyChannelReturnsDefault)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::LINEAR;
    // Empty timestamps and values

    glm::vec3 result = sampleVec3(ch, 0.5f);
    EXPECT_NEAR(result.x, 0.0f, 0.001f);
    EXPECT_NEAR(result.y, 0.0f, 0.001f);
    EXPECT_NEAR(result.z, 0.0f, 0.001f);
}

TEST(AnimSamplerTest, SingleKeyframeReturnsValue)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::LINEAR;
    ch.timestamps = {0.5f};
    ch.values = {7.0f, 8.0f, 9.0f};

    glm::vec3 result = sampleVec3(ch, 100.0f);
    EXPECT_NEAR(result.x, 7.0f, 0.001f);
    EXPECT_NEAR(result.y, 8.0f, 0.001f);
    EXPECT_NEAR(result.z, 9.0f, 0.001f);
}

TEST(AnimSamplerTest, ThreeKeyframeLinear)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::LINEAR;
    ch.timestamps = {0.0f, 1.0f, 2.0f};
    ch.values = {0, 0, 0,   10, 0, 0,   10, 10, 0};

    // Between keyframe 0 and 1
    glm::vec3 r1 = sampleVec3(ch, 0.5f);
    EXPECT_NEAR(r1.x, 5.0f, 0.001f);
    EXPECT_NEAR(r1.y, 0.0f, 0.001f);

    // Between keyframe 1 and 2
    glm::vec3 r2 = sampleVec3(ch, 1.5f);
    EXPECT_NEAR(r2.x, 10.0f, 0.001f);
    EXPECT_NEAR(r2.y, 5.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// AUDIT A2 — CUBICSPLINE quaternion double-cover fix.
// Two adjacent keyframes whose quaternions are antipodal (q and -q) must
// not blend through the origin: the sampler must flip vk1 (and ak1) into
// vk's hemisphere before the Hermite blend. With zero tangents the blend
// degenerates to component-wise lerp, which makes the bug observable —
// pre-A2 the midpoint was the zero quaternion (length 0).
// ---------------------------------------------------------------------------
TEST(AnimSamplerTest, CubicSplineQuatHandlesAntipodalKeyframes_A2)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::ROTATION;
    ch.interpolation = AnimInterpolation::CUBICSPLINE;
    ch.timestamps = {0.0f, 1.0f};

    glm::quat q0 = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));
    glm::quat q1 = -q0;  // Antipodal — same rotation, opposite hemisphere.

    // CUBICSPLINE layout per glTF: 3 vec4s per keyframe = [in-tangent, value, out-tangent].
    // Zero tangents on both ends so the result is a pure component-wise lerp.
    ch.values = {
        // Keyframe 0
        0, 0, 0, 0,                  // in-tangent
        q0.x, q0.y, q0.z, q0.w,      // value
        0, 0, 0, 0,                  // out-tangent
        // Keyframe 1
        0, 0, 0, 0,                  // in-tangent
        q1.x, q1.y, q1.z, q1.w,      // value
        0, 0, 0, 0,                  // out-tangent
    };

    glm::quat midpoint = sampleQuat(ch, 0.5f);

    // After hemisphere alignment the midpoint is q0 (or its normalised
    // equivalent) — not the zero quaternion.
    EXPECT_NEAR(std::fabs(midpoint.w), std::fabs(q0.w), 1e-4f);
    EXPECT_NEAR(std::fabs(midpoint.x), std::fabs(q0.x), 1e-4f);
    EXPECT_NEAR(std::fabs(midpoint.y), std::fabs(q0.y), 1e-4f);
    EXPECT_NEAR(std::fabs(midpoint.z), std::fabs(q0.z), 1e-4f);

    const float len = std::sqrt(midpoint.w * midpoint.w + midpoint.x * midpoint.x
                              + midpoint.y * midpoint.y + midpoint.z * midpoint.z);
    EXPECT_NEAR(len, 1.0f, 1e-4f);
}

TEST(AnimSamplerTest, CubicSplineQuatSameHemisphereStillSmooth_A2)
{
    // Control: same setup but with q1 already in q0's hemisphere. The fix
    // must be a no-op here — output is q0-ish at the midpoint.
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::ROTATION;
    ch.interpolation = AnimInterpolation::CUBICSPLINE;
    ch.timestamps = {0.0f, 1.0f};

    glm::quat q0 = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));
    glm::quat q1 = q0;  // Same hemisphere.

    ch.values = {
        0, 0, 0, 0,
        q0.x, q0.y, q0.z, q0.w,
        0, 0, 0, 0,
        0, 0, 0, 0,
        q1.x, q1.y, q1.z, q1.w,
        0, 0, 0, 0,
    };

    glm::quat midpoint = sampleQuat(ch, 0.5f);
    EXPECT_NEAR(midpoint.w, q0.w, 1e-4f);
    EXPECT_NEAR(midpoint.x, q0.x, 1e-4f);
    EXPECT_NEAR(midpoint.y, q0.y, 1e-4f);
    EXPECT_NEAR(midpoint.z, q0.z, 1e-4f);
}
