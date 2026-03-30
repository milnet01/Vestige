/// @file test_cubicspline.cpp
/// @brief Unit tests for CUBICSPLINE interpolation in animation sampling.
#include "animation/animation_sampler.h"
#include "animation/animation_clip.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helper: build a CUBICSPLINE vec3 channel with 2 keyframes.
// Each keyframe stores 3 vec3s (9 floats): [in-tangent, value, out-tangent]
// ---------------------------------------------------------------------------
static AnimationChannel makeCubicVec3Channel(
    float t0, float t1,
    glm::vec3 inTan0, glm::vec3 val0, glm::vec3 outTan0,
    glm::vec3 inTan1, glm::vec3 val1, glm::vec3 outTan1)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::CUBICSPLINE;
    ch.timestamps = {t0, t1};
    ch.values = {
        // Keyframe 0: in-tangent, value, out-tangent
        inTan0.x, inTan0.y, inTan0.z,
        val0.x,   val0.y,   val0.z,
        outTan0.x, outTan0.y, outTan0.z,
        // Keyframe 1: in-tangent, value, out-tangent
        inTan1.x, inTan1.y, inTan1.z,
        val1.x,   val1.y,   val1.z,
        outTan1.x, outTan1.y, outTan1.z
    };
    return ch;
}

// ---------------------------------------------------------------------------
// Helper: build a CUBICSPLINE quat channel with 2 keyframes.
// Each keyframe stores 3 vec4s (12 floats): [in-tangent, value, out-tangent]
// glTF stores quats as (x, y, z, w)
// ---------------------------------------------------------------------------
static AnimationChannel makeCubicQuatChannel(
    float t0, float t1,
    glm::quat inTan0, glm::quat val0, glm::quat outTan0,
    glm::quat inTan1, glm::quat val1, glm::quat outTan1)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::ROTATION;
    ch.interpolation = AnimInterpolation::CUBICSPLINE;
    ch.timestamps = {t0, t1};

    // Store as (x, y, z, w) per glTF convention
    auto pushQuat = [&](const glm::quat& q)
    {
        ch.values.push_back(q.x);
        ch.values.push_back(q.y);
        ch.values.push_back(q.z);
        ch.values.push_back(q.w);
    };

    // Keyframe 0
    pushQuat(inTan0);
    pushQuat(val0);
    pushQuat(outTan0);
    // Keyframe 1
    pushQuat(inTan1);
    pushQuat(val1);
    pushQuat(outTan1);

    return ch;
}

// ---------------------------------------------------------------------------
// Tests: CUBICSPLINE vec3
// ---------------------------------------------------------------------------

TEST(CubicSplineTest, Vec3AtStart)
{
    // Zero tangents — should return val0 at t=0
    auto ch = makeCubicVec3Channel(
        0.0f, 1.0f,
        {0, 0, 0}, {1, 2, 3}, {0, 0, 0},  // key 0
        {0, 0, 0}, {4, 5, 6}, {0, 0, 0}   // key 1
    );
    glm::vec3 r = sampleVec3(ch, 0.0f);
    EXPECT_NEAR(r.x, 1.0f, 0.001f);
    EXPECT_NEAR(r.y, 2.0f, 0.001f);
    EXPECT_NEAR(r.z, 3.0f, 0.001f);
}

TEST(CubicSplineTest, Vec3AtEnd)
{
    auto ch = makeCubicVec3Channel(
        0.0f, 1.0f,
        {0, 0, 0}, {1, 2, 3}, {0, 0, 0},
        {0, 0, 0}, {4, 5, 6}, {0, 0, 0}
    );
    glm::vec3 r = sampleVec3(ch, 1.0f);
    EXPECT_NEAR(r.x, 4.0f, 0.001f);
    EXPECT_NEAR(r.y, 5.0f, 0.001f);
    EXPECT_NEAR(r.z, 6.0f, 0.001f);
}

TEST(CubicSplineTest, Vec3ZeroTangentsMidpoint)
{
    // With zero tangents, cubic Hermite at s=0.5:
    // p(0.5) = (2*0.125 - 3*0.25 + 1)*v0 + (-2*0.125 + 3*0.25)*v1
    //        = (0.25 - 0.75 + 1)*v0 + (-0.25 + 0.75)*v1
    //        = 0.5*v0 + 0.5*v1  (same as linear when tangents are zero)
    auto ch = makeCubicVec3Channel(
        0.0f, 1.0f,
        {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
        {0, 0, 0}, {10, 20, 30}, {0, 0, 0}
    );
    glm::vec3 r = sampleVec3(ch, 0.5f);
    EXPECT_NEAR(r.x, 5.0f, 0.01f);
    EXPECT_NEAR(r.y, 10.0f, 0.01f);
    EXPECT_NEAR(r.z, 15.0f, 0.01f);
}

TEST(CubicSplineTest, Vec3WithTangents)
{
    // Out-tangent at key 0 = (6,0,0), in-tangent at key 1 = (6,0,0)
    // t0=0, t1=1, deltaTime=1
    // At s=0.5: p = 0.5*v0 + (0.125 - 0.5 + 0.5)*1*outTan0 + 0.5*v1 + (0.125 - 0.25)*1*inTan1
    //         = 0.5*0 + 0.125*6 + 0.5*10 + (-0.125)*6 = 0 + 0.75 + 5 - 0.75 = 5.0 (x)
    auto ch = makeCubicVec3Channel(
        0.0f, 1.0f,
        {0, 0, 0}, {0, 0, 0}, {6, 0, 0},   // key 0: zero in, value=0, out-tangent=(6,0,0)
        {6, 0, 0}, {10, 0, 0}, {0, 0, 0}    // key 1: in-tangent=(6,0,0), value=10, zero out
    );
    glm::vec3 r = sampleVec3(ch, 0.5f);
    // With symmetric tangents pointing in the direction of travel,
    // midpoint should still be ~5.0 (symmetric S-curve)
    EXPECT_NEAR(r.x, 5.0f, 0.01f);
}

TEST(CubicSplineTest, Vec3BeforeFirst)
{
    auto ch = makeCubicVec3Channel(
        1.0f, 2.0f,
        {0, 0, 0}, {1, 2, 3}, {0, 0, 0},
        {0, 0, 0}, {4, 5, 6}, {0, 0, 0}
    );
    glm::vec3 r = sampleVec3(ch, 0.0f);
    EXPECT_NEAR(r.x, 1.0f, 0.001f);
    EXPECT_NEAR(r.y, 2.0f, 0.001f);
}

TEST(CubicSplineTest, Vec3AfterLast)
{
    auto ch = makeCubicVec3Channel(
        0.0f, 1.0f,
        {0, 0, 0}, {1, 2, 3}, {0, 0, 0},
        {0, 0, 0}, {4, 5, 6}, {0, 0, 0}
    );
    glm::vec3 r = sampleVec3(ch, 5.0f);
    EXPECT_NEAR(r.x, 4.0f, 0.001f);
    EXPECT_NEAR(r.y, 5.0f, 0.001f);
}

TEST(CubicSplineTest, Vec3SingleKeyframe)
{
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::CUBICSPLINE;
    ch.timestamps = {0.5f};
    ch.values = {
        0, 0, 0,    // in-tangent
        7, 8, 9,    // value
        0, 0, 0     // out-tangent
    };
    glm::vec3 r = sampleVec3(ch, 0.5f);
    EXPECT_NEAR(r.x, 7.0f, 0.001f);
    EXPECT_NEAR(r.y, 8.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Tests: CUBICSPLINE quat
// ---------------------------------------------------------------------------

TEST(CubicSplineTest, QuatAtStartIsNormalized)
{
    glm::quat identity(1, 0, 0, 0);
    glm::quat zeroTan(0, 0, 0, 0);
    auto ch = makeCubicQuatChannel(
        0.0f, 1.0f,
        zeroTan, identity, zeroTan,
        zeroTan, identity, zeroTan
    );
    glm::quat r = sampleQuat(ch, 0.0f);
    float len = glm::length(r);
    EXPECT_NEAR(len, 1.0f, 0.001f);
}

TEST(CubicSplineTest, QuatResultIsNormalized)
{
    glm::quat q0(1, 0, 0, 0);
    glm::quat q1 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    glm::quat zeroTan(0, 0, 0, 0);
    auto ch = makeCubicQuatChannel(
        0.0f, 1.0f,
        zeroTan, q0, zeroTan,
        zeroTan, q1, zeroTan
    );
    glm::quat r = sampleQuat(ch, 0.5f);
    float len = glm::length(r);
    EXPECT_NEAR(len, 1.0f, 0.001f);
}

TEST(CubicSplineTest, QuatZeroTangentsInterpolatesSmoothly)
{
    glm::quat q0(1, 0, 0, 0);  // identity
    glm::quat q1 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    glm::quat zeroTan(0, 0, 0, 0);
    auto ch = makeCubicQuatChannel(
        0.0f, 1.0f,
        zeroTan, q0, zeroTan,
        zeroTan, q1, zeroTan
    );

    // At start should be ~identity
    glm::quat r0 = sampleQuat(ch, 0.0f);
    EXPECT_NEAR(r0.w, 1.0f, 0.01f);

    // At end should be ~q1
    glm::quat r1 = sampleQuat(ch, 1.0f);
    EXPECT_NEAR(glm::dot(r1, q1), 1.0f, 0.01f);

    // At midpoint: should be between q0 and q1 (~45 degrees)
    glm::quat rMid = sampleQuat(ch, 0.5f);
    float dotStart = std::abs(glm::dot(rMid, q0));
    float dotEnd = std::abs(glm::dot(rMid, q1));
    // Should be closer to neither extreme
    EXPECT_LT(dotStart, 0.99f);
    EXPECT_LT(dotEnd, 0.99f);
}
