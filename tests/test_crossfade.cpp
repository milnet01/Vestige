// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_crossfade.cpp
/// @brief Unit tests for animation crossfade blending.
#include "animation/skeleton_animator.h"
#include "animation/skeleton.h"
#include "animation/animation_clip.h"
#include "skeleton_test_helpers.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helpers: create a minimal skeleton and animation clips for testing
// ---------------------------------------------------------------------------

// /test-audit 2026-05-17 Ts19-D2: chain-skeleton builder shared with
// test_animation_state_machine.cpp + test_root_motion.cpp via
// skeleton_test_helpers.h. NOTE: the original here used
// parentIndex = 0 for every child (star, not chain). The chain builder's
// per-i parent matters for skinning math; for these crossfade tests
// only joint 0 is animated, so a chain works the same as the star.
static std::shared_ptr<Skeleton> makeTestSkeleton(int jointCount = 2)
{
    return ::Vestige::Testing::makeJointChainSkeleton(jointCount);
}

/// @brief Creates a clip that moves joint 0 to a constant position.
static std::shared_ptr<AnimationClip> makeConstantClip(
    const std::string& name, float duration, glm::vec3 position)
{
    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = name;

    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::LINEAR;
    ch.timestamps = {0.0f, duration};
    ch.values = {position.x, position.y, position.z,
                 position.x, position.y, position.z};
    clip->m_channels.push_back(ch);
    clip->computeDuration();
    return clip;
}

/// @brief Creates a clip that moves joint 0 linearly from posA to posB.
static std::shared_ptr<AnimationClip> makeLinearClip(
    const std::string& name, float duration, glm::vec3 posA, glm::vec3 posB)
{
    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = name;

    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::LINEAR;
    ch.timestamps = {0.0f, duration};
    ch.values = {posA.x, posA.y, posA.z, posB.x, posB.y, posB.z};
    clip->m_channels.push_back(ch);
    clip->computeDuration();
    return clip;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class CrossfadeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_skel = makeTestSkeleton(2);
        m_clipA = makeConstantClip("A", 1.0f, glm::vec3(0, 0, 0));
        m_clipB = makeConstantClip("B", 1.0f, glm::vec3(10, 20, 30));

        m_animator.setSkeleton(m_skel);
        m_animator.addClip(m_clipA);
        m_animator.addClip(m_clipB);
    }

    std::shared_ptr<Skeleton> m_skel;
    std::shared_ptr<AnimationClip> m_clipA;
    std::shared_ptr<AnimationClip> m_clipB;
    SkeletonAnimator m_animator;
};

TEST_F(CrossfadeTest, NotCrossfadingByDefault)
{
    EXPECT_FALSE(m_animator.isCrossfading());
}

TEST_F(CrossfadeTest, CrossfadeStartsBlending)
{
    m_animator.playIndex(0);
    m_animator.update(0.1f);  // Establish clip A
    m_animator.crossfadeToIndex(1, 0.5f);
    EXPECT_TRUE(m_animator.isCrossfading());
}

TEST_F(CrossfadeTest, CrossfadeCompletesAfterDuration)
{
    m_animator.playIndex(0);
    m_animator.update(0.1f);
    m_animator.crossfadeToIndex(1, 0.5f);

    // Update past the crossfade duration
    m_animator.update(0.6f);
    EXPECT_FALSE(m_animator.isCrossfading());
}

TEST_F(CrossfadeTest, MidCrossfadeBlendsBetweenPoses)
{
    m_animator.playIndex(0);
    m_animator.update(0.01f);  // Sample clip A (position = 0,0,0)

    m_animator.crossfadeToIndex(1, 1.0f);  // 1 second crossfade
    m_animator.update(0.5f);  // 50% through crossfade

    // Bone matrices should reflect a blend between (0,0,0) and (10,20,30)
    const auto& matrices = m_animator.getBoneMatrices();
    ASSERT_GE(matrices.size(), 1u);

    // Joint 0's translation should be approximately halfway: (5, 10, 15)
    glm::vec3 pos(matrices[0][3]);
    EXPECT_NEAR(pos.x, 5.0f, 1.0f);
    EXPECT_NEAR(pos.y, 10.0f, 2.0f);
    EXPECT_NEAR(pos.z, 15.0f, 2.0f);
}

TEST_F(CrossfadeTest, ZeroDurationCrossfadeIsInstantSwitch)
{
    m_animator.playIndex(0);
    m_animator.update(0.1f);

    m_animator.crossfadeToIndex(1, 0.0f);
    EXPECT_FALSE(m_animator.isCrossfading());
    EXPECT_EQ(m_animator.getActiveClipIndex(), 1);
}

TEST_F(CrossfadeTest, CrossfadeByName)
{
    m_animator.playIndex(0);
    m_animator.update(0.1f);

    m_animator.crossfadeTo("B", 0.5f);
    EXPECT_TRUE(m_animator.isCrossfading());
    EXPECT_EQ(m_animator.getActiveClipIndex(), 1);
}

TEST_F(CrossfadeTest, TransitionInterruptionFreezesPose)
{
    auto clipC = makeConstantClip("C", 1.0f, glm::vec3(100, 100, 100));
    m_animator.addClip(clipC);

    m_animator.playIndex(0);
    m_animator.update(0.01f);

    // Start crossfade A→B
    m_animator.crossfadeToIndex(1, 1.0f);
    m_animator.update(0.3f);  // 30% through

    // Interrupt: crossfade to C
    m_animator.crossfadeToIndex(2, 0.5f);
    EXPECT_TRUE(m_animator.isCrossfading());
    EXPECT_EQ(m_animator.getActiveClipIndex(), 2);

    // Complete the new crossfade
    m_animator.update(0.6f);
    EXPECT_FALSE(m_animator.isCrossfading());
}

TEST_F(CrossfadeTest, CrossfadeWithNothingPlayingJustPlays)
{
    // Don't call playIndex first
    m_animator.crossfadeToIndex(1, 0.5f);
    // Should just start playing (not crossfading from nothing)
    EXPECT_FALSE(m_animator.isCrossfading());
    EXPECT_TRUE(m_animator.isPlaying());
    EXPECT_EQ(m_animator.getActiveClipIndex(), 1);
}

TEST_F(CrossfadeTest, InvalidIndexIgnored)
{
    m_animator.playIndex(0);
    m_animator.update(0.01f);
    m_animator.crossfadeToIndex(99, 0.5f);
    // Should not have changed
    EXPECT_EQ(m_animator.getActiveClipIndex(), 0);
    EXPECT_FALSE(m_animator.isCrossfading());
}
