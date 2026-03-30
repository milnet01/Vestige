/// @file test_ik_solver.cpp
/// @brief Unit tests for inverse kinematics solvers.
#include "animation/ik_solver.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Two-Bone IK Tests
// ---------------------------------------------------------------------------

/// @brief Helper: create a straight vertical chain (start=origin, mid=up, end=further up).
static TwoBoneIKRequest makeVerticalChain(const glm::vec3& target)
{
    TwoBoneIKRequest req;
    req.startPos = glm::vec3(0.0f, 0.0f, 0.0f);
    req.midPos   = glm::vec3(0.0f, 1.0f, 0.0f);
    req.endPos   = glm::vec3(0.0f, 2.0f, 0.0f);
    req.startGlobalRot = glm::quat(1, 0, 0, 0);
    req.midGlobalRot   = glm::quat(1, 0, 0, 0);
    req.startLocalRot  = glm::quat(1, 0, 0, 0);
    req.midLocalRot    = glm::quat(1, 0, 0, 0);
    req.target = target;
    req.poleVector = glm::vec3(0.0f, 0.0f, 1.0f);
    req.weight = 1.0f;
    return req;
}

TEST(TwoBoneIKTest, ReachableTarget)
{
    auto req = makeVerticalChain(glm::vec3(1.0f, 1.0f, 0.0f));
    TwoBoneIKResult result = solveTwoBoneIK(req);

    // Should produce changed rotations (not identity)
    glm::quat identity(1, 0, 0, 0);
    bool startChanged = glm::dot(result.startLocalRot, identity) < 0.999f;
    bool midChanged = glm::dot(result.midLocalRot, identity) < 0.999f;
    EXPECT_TRUE(startChanged || midChanged);
}

TEST(TwoBoneIKTest, TargetAtEndEffector)
{
    // Target is already at the end effector — minimal correction
    auto req = makeVerticalChain(glm::vec3(0.0f, 2.0f, 0.0f));
    TwoBoneIKResult result = solveTwoBoneIK(req);

    EXPECT_TRUE(result.reached);
    // Rotations should be near identity (already there)
    EXPECT_NEAR(result.startLocalRot.w, 1.0f, 0.05f);
    EXPECT_NEAR(result.midLocalRot.w, 1.0f, 0.05f);
}

TEST(TwoBoneIKTest, UnreachableTarget)
{
    // Target far away (chain length = 2, target at distance 10)
    auto req = makeVerticalChain(glm::vec3(10.0f, 0.0f, 0.0f));
    TwoBoneIKResult result = solveTwoBoneIK(req);

    EXPECT_FALSE(result.reached);
    // Should still produce valid quaternions
    EXPECT_NEAR(glm::length(glm::vec4(result.startLocalRot.x, result.startLocalRot.y,
                                       result.startLocalRot.z, result.startLocalRot.w)), 1.0f, 0.01f);
}

TEST(TwoBoneIKTest, TargetAtStartPosition)
{
    auto req = makeVerticalChain(glm::vec3(0.0f, 0.0f, 0.0f));
    TwoBoneIKResult result = solveTwoBoneIK(req);

    // Should not crash, should produce valid quaternions
    float len = glm::length(glm::vec4(result.startLocalRot.x, result.startLocalRot.y,
                                       result.startLocalRot.z, result.startLocalRot.w));
    EXPECT_NEAR(len, 1.0f, 0.01f);
}

TEST(TwoBoneIKTest, WeightZeroReturnsOriginal)
{
    auto req = makeVerticalChain(glm::vec3(1.0f, 1.0f, 0.0f));
    req.weight = 0.0f;
    TwoBoneIKResult result = solveTwoBoneIK(req);

    EXPECT_NEAR(result.startLocalRot.w, 1.0f, 0.001f);
    EXPECT_NEAR(result.midLocalRot.w, 1.0f, 0.001f);
}

TEST(TwoBoneIKTest, WeightHalfProducesIntermediate)
{
    auto req = makeVerticalChain(glm::vec3(1.0f, 1.0f, 0.0f));
    req.weight = 0.5f;

    auto fullResult = solveTwoBoneIK(makeVerticalChain(glm::vec3(1.0f, 1.0f, 0.0f)));
    auto halfResult = solveTwoBoneIK(req);

    // Half weight should be between identity and full
    glm::quat identity(1, 0, 0, 0);
    float dotFull = std::abs(glm::dot(fullResult.startLocalRot, identity));
    float dotHalf = std::abs(glm::dot(halfResult.startLocalRot, identity));

    // Half should be closer to identity than full
    EXPECT_GT(dotHalf, dotFull - 0.01f);
}

TEST(TwoBoneIKTest, DifferentBoneLengths)
{
    TwoBoneIKRequest req;
    req.startPos = glm::vec3(0.0f);
    req.midPos   = glm::vec3(0.0f, 0.5f, 0.0f);  // Short upper bone
    req.endPos   = glm::vec3(0.0f, 2.0f, 0.0f);   // Long lower bone
    req.startGlobalRot = glm::quat(1, 0, 0, 0);
    req.midGlobalRot   = glm::quat(1, 0, 0, 0);
    req.startLocalRot  = glm::quat(1, 0, 0, 0);
    req.midLocalRot    = glm::quat(1, 0, 0, 0);
    req.target = glm::vec3(1.0f, 1.0f, 0.0f);
    req.poleVector = glm::vec3(0.0f, 0.0f, 1.0f);
    req.weight = 1.0f;

    TwoBoneIKResult result = solveTwoBoneIK(req);

    // Should handle asymmetric bones without crashing
    float len = glm::length(glm::vec4(result.startLocalRot.x, result.startLocalRot.y,
                                       result.startLocalRot.z, result.startLocalRot.w));
    EXPECT_NEAR(len, 1.0f, 0.01f);
}

TEST(TwoBoneIKTest, ResultQuaternionsNormalized)
{
    auto req = makeVerticalChain(glm::vec3(0.5f, 1.5f, 0.3f));
    TwoBoneIKResult result = solveTwoBoneIK(req);

    float startLen = glm::length(glm::vec4(result.startLocalRot.x, result.startLocalRot.y,
                                            result.startLocalRot.z, result.startLocalRot.w));
    float midLen = glm::length(glm::vec4(result.midLocalRot.x, result.midLocalRot.y,
                                          result.midLocalRot.z, result.midLocalRot.w));
    EXPECT_NEAR(startLen, 1.0f, 0.01f);
    EXPECT_NEAR(midLen, 1.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Look-At IK Tests
// ---------------------------------------------------------------------------

/// @brief Helper: joint at origin facing -Z.
static LookAtIKRequest makeLookAtReq(const glm::vec3& target)
{
    LookAtIKRequest req;
    req.jointPos = glm::vec3(0.0f);
    req.jointGlobalRot = glm::quat(1, 0, 0, 0);
    req.jointLocalRot = glm::quat(1, 0, 0, 0);
    req.target = target;
    req.forwardAxis = glm::vec3(0.0f, 0.0f, -1.0f);
    req.maxAngle = glm::radians(90.0f);
    req.weight = 1.0f;
    return req;
}

TEST(LookAtIKTest, RotatesToFaceTarget)
{
    auto req = makeLookAtReq(glm::vec3(1.0f, 0.0f, 0.0f)); // Target to the right
    LookAtIKResult result = solveLookAtIK(req);

    // Forward should now point roughly toward +X
    glm::vec3 newForward = result.localRot * req.forwardAxis;
    EXPECT_GT(newForward.x, 0.5f);
}

TEST(LookAtIKTest, AlreadyFacingTarget)
{
    auto req = makeLookAtReq(glm::vec3(0.0f, 0.0f, -5.0f)); // Already facing -Z
    LookAtIKResult result = solveLookAtIK(req);

    // Should be near identity (already looking at target)
    EXPECT_NEAR(result.localRot.w, 1.0f, 0.01f);
}

TEST(LookAtIKTest, MaxAngleConstraint)
{
    auto req = makeLookAtReq(glm::vec3(0.0f, 0.0f, 5.0f)); // Target behind (180 degrees)
    req.maxAngle = glm::radians(45.0f);
    LookAtIKResult result = solveLookAtIK(req);

    // The actual angle from identity should be <= maxAngle
    float cw = std::abs(result.localRot.w);
    if (cw > 1.0f) cw = 1.0f;
    float angle = 2.0f * std::acos(cw);
    EXPECT_LE(angle, glm::radians(45.0f) + 0.01f);
}

TEST(LookAtIKTest, WeightZeroReturnsOriginal)
{
    auto req = makeLookAtReq(glm::vec3(1.0f, 0.0f, 0.0f));
    req.weight = 0.0f;
    LookAtIKResult result = solveLookAtIK(req);

    EXPECT_NEAR(result.localRot.w, 1.0f, 0.001f);
}

TEST(LookAtIKTest, TargetAtJointPosition)
{
    auto req = makeLookAtReq(glm::vec3(0.0f, 0.0f, 0.0f));
    LookAtIKResult result = solveLookAtIK(req);

    // Should return original (can't look at yourself)
    EXPECT_NEAR(result.localRot.w, req.jointLocalRot.w, 0.001f);
}

// ---------------------------------------------------------------------------
// Foot IK Tests
// ---------------------------------------------------------------------------

/// @brief Helper: standing leg chain (hip at 2, knee at 1, ankle at 0).
static FootIKRequest makeFootReq(const glm::vec3& groundPos, const glm::vec3& groundNormal)
{
    FootIKRequest req;
    req.hipPos    = glm::vec3(0.0f, 2.0f, 0.0f);
    req.kneePos   = glm::vec3(0.0f, 1.0f, 0.0f);
    req.anklePos  = glm::vec3(0.0f, 0.0f, 0.0f);
    req.hipGlobalRot   = glm::quat(1, 0, 0, 0);
    req.kneeGlobalRot  = glm::quat(1, 0, 0, 0);
    req.ankleGlobalRot = glm::quat(1, 0, 0, 0);
    req.hipLocalRot    = glm::quat(1, 0, 0, 0);
    req.kneeLocalRot   = glm::quat(1, 0, 0, 0);
    req.ankleLocalRot  = glm::quat(1, 0, 0, 0);
    req.groundPosition = groundPos;
    req.groundNormal   = groundNormal;
    req.weight = 1.0f;
    return req;
}

TEST(FootIKTest, FlatGroundNoPelvisOffset)
{
    // Ankle is already at Y=0, ground at Y=0
    auto req = makeFootReq(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    FootIKResult result = solveFootIK(req);

    EXPECT_NEAR(result.pelvisOffset, 0.0f, 0.01f);
}

TEST(FootIKTest, LowerGroundPelvisDrops)
{
    // Ground is below ankle by 0.5
    auto req = makeFootReq(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    FootIKResult result = solveFootIK(req);

    EXPECT_NEAR(result.pelvisOffset, -0.5f, 0.01f);
}

TEST(FootIKTest, HigherGroundPelvisRises)
{
    // Ground is above ankle by 0.3
    auto req = makeFootReq(glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    FootIKResult result = solveFootIK(req);

    EXPECT_NEAR(result.pelvisOffset, 0.3f, 0.01f);
}

TEST(FootIKTest, SlopedGroundAlignsAnkle)
{
    // Ground tilted 45 degrees
    glm::vec3 slopeNormal = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    auto req = makeFootReq(glm::vec3(0.0f, 0.0f, 0.0f), slopeNormal);
    FootIKResult result = solveFootIK(req);

    // Ankle rotation should differ from identity (rotated to match slope)
    float dotId = std::abs(glm::dot(result.ankleLocalRot, glm::quat(1, 0, 0, 0)));
    EXPECT_LT(dotId, 0.99f);
}

TEST(FootIKTest, WeightZeroNoChange)
{
    auto req = makeFootReq(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    req.weight = 0.0f;
    FootIKResult result = solveFootIK(req);

    EXPECT_NEAR(result.pelvisOffset, 0.0f, 0.001f);
    EXPECT_NEAR(result.hipLocalRot.w, 1.0f, 0.001f);
}
