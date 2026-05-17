// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_root_motion.cpp
/// @brief Unit tests for root motion extraction.
#include "animation/skeleton_animator.h"
#include "animation/skeleton.h"
#include "animation/animation_clip.h"
#include "skeleton_test_helpers.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// /test-audit 2026-05-17 Ts19-D2: chain-skeleton builder shared with
// test_animation_state_machine.cpp + test_crossfade.cpp via
// skeleton_test_helpers.h. The +Y child offset matters for the
// root-motion delta math, so it's passed explicitly.
static std::shared_ptr<Skeleton> makeTestSkeleton()
{
    return ::Vestige::Testing::makeJointChainSkeleton(2, glm::vec3(0, 1, 0));
}

/// @brief Creates a clip where joint 0 moves from (0,0,0) to (10,0,0) over duration.
static std::shared_ptr<AnimationClip> makeWalkClip(float duration)
{
    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = "walk";

    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::LINEAR;
    ch.timestamps = {0.0f, duration};
    ch.values = {0, 0, 0,  10, 0, 0};  // move 10 units along X
    clip->m_channels.push_back(ch);
    clip->computeDuration();
    return clip;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class RootMotionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_skel = makeTestSkeleton();
        m_clip = makeWalkClip(1.0f);

        m_animator.setSkeleton(m_skel);
        m_animator.addClip(m_clip);
    }

    std::shared_ptr<Skeleton> m_skel;
    std::shared_ptr<AnimationClip> m_clip;
    SkeletonAnimator m_animator;
};

TEST_F(RootMotionTest, DefaultModeIsIgnore)
{
    EXPECT_EQ(m_animator.getRootMotionMode(), RootMotionMode::IGNORE);
}

TEST_F(RootMotionTest, NoDeltaWhenIgnored)
{
    m_animator.playIndex(0);
    m_animator.update(0.5f);

    glm::vec3 delta = m_animator.getRootMotionDeltaPosition();
    EXPECT_NEAR(delta.x, 0.0f, 0.001f);
    EXPECT_NEAR(delta.y, 0.0f, 0.001f);
    EXPECT_NEAR(delta.z, 0.0f, 0.001f);
}

TEST_F(RootMotionTest, DeltaExtractedWhenEnabled)
{
    m_animator.setRootMotionMode(RootMotionMode::APPLY_TO_TRANSFORM);
    m_animator.setRootMotionBone(0);
    m_animator.playIndex(0);

    // First update: initializes baseline, no meaningful delta yet
    m_animator.update(0.1f);

    // Second update: should have a delta
    m_animator.update(0.1f);
    glm::vec3 delta = m_animator.getRootMotionDeltaPosition();

    // The clip linearly interpolates 10 units over 1 second; a 0.1s step is
    // exactly 1.0 (linear interpolation has zero approximation error). 1e-3
    // tolerance catches float-quantisation; the old 0.2 band would have
    // accepted a 20% scale drift in the root-motion extractor.
    EXPECT_NEAR(delta.x, 1.0f, 1e-3f);
    EXPECT_NEAR(delta.y, 0.0f, 0.001f);
    EXPECT_NEAR(delta.z, 0.0f, 0.001f);
}

TEST_F(RootMotionTest, RootBoneZeroedAfterExtraction)
{
    m_animator.setRootMotionMode(RootMotionMode::APPLY_TO_TRANSFORM);
    m_animator.setRootMotionBone(0);
    m_animator.playIndex(0);

    m_animator.update(0.5f);  // halfway through animation

    // Bone matrices should show root bone at origin (X/Z zeroed)
    const auto& matrices = m_animator.getBoneMatrices();
    ASSERT_GE(matrices.size(), 1u);

    // Root bone X position should be 0 (motion extracted)
    glm::vec3 rootPos(matrices[0][3]);
    EXPECT_NEAR(rootPos.x, 0.0f, 0.01f);
    EXPECT_NEAR(rootPos.z, 0.0f, 0.01f);
}

TEST_F(RootMotionTest, DeltaRotationIsIdentityForTranslationOnly)
{
    m_animator.setRootMotionMode(RootMotionMode::APPLY_TO_TRANSFORM);
    m_animator.playIndex(0);
    m_animator.update(0.1f);
    m_animator.update(0.1f);

    glm::quat deltaRot = m_animator.getRootMotionDeltaRotation();
    // Should be identity-ish (no rotation in the clip)
    EXPECT_NEAR(deltaRot.w, 1.0f, 0.01f);
    EXPECT_NEAR(deltaRot.x, 0.0f, 0.01f);
    EXPECT_NEAR(deltaRot.y, 0.0f, 0.01f);
    EXPECT_NEAR(deltaRot.z, 0.0f, 0.01f);
}

TEST_F(RootMotionTest, DeltaAccumulatesAcrossFrames)
{
    m_animator.setRootMotionMode(RootMotionMode::APPLY_TO_TRANSFORM);
    m_animator.setLooping(false);
    m_animator.playIndex(0);

    glm::vec3 totalDelta(0.0f);

    // First frame: init baseline (delta is 0 on this frame)
    m_animator.update(0.1f);
    totalDelta += m_animator.getRootMotionDeltaPosition();

    // Subsequent 9 frames: each produces ~1 unit of delta
    for (int i = 0; i < 9; ++i)
    {
        m_animator.update(0.1f);
        totalDelta += m_animator.getRootMotionDeltaPosition();
    }

    // First frame has no delta (baseline init), so 9 frames × 1 unit = 9 units.
    // Linear interpolation is exact; tolerance covers float-summation noise only.
    EXPECT_NEAR(totalDelta.x, 9.0f, 1e-3f);
}

TEST_F(RootMotionTest, NoDeltaWhenNotPlaying)
{
    m_animator.setRootMotionMode(RootMotionMode::APPLY_TO_TRANSFORM);
    // Don't call playIndex
    m_animator.update(0.1f);

    glm::vec3 delta = m_animator.getRootMotionDeltaPosition();
    EXPECT_NEAR(delta.x, 0.0f, 0.001f);
}

TEST_F(RootMotionTest, SetModeResetsInitialization)
{
    m_animator.setRootMotionMode(RootMotionMode::APPLY_TO_TRANSFORM);
    m_animator.playIndex(0);
    m_animator.update(0.5f);

    // Change mode — should re-initialize
    m_animator.setRootMotionMode(RootMotionMode::APPLY_TO_TRANSFORM);
    m_animator.update(0.1f);
    // First frame after reset: baseline init, delta should be minimal
    glm::vec3 delta = m_animator.getRootMotionDeltaPosition();
    // On first frame after re-init, delta is 0 (baseline capture)
    EXPECT_NEAR(delta.x, 0.0f, 0.001f);
}
