// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_skeleton_animator.cpp
/// @brief Unit tests for SkeletonAnimator component.
#include "animation/skeleton_animator.h"
#include "animation/skeleton.h"
#include "animation/animation_clip.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Skeleton> makeSimpleSkeleton()
{
    auto skeleton = std::make_shared<Skeleton>();

    Joint root;
    root.name = "Root";
    root.parentIndex = -1;
    root.inverseBindMatrix = glm::mat4(1.0f);
    root.localBindTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0));

    Joint child;
    child.name = "Child";
    child.parentIndex = 0;
    child.inverseBindMatrix = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)));
    child.localBindTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0));

    skeleton->m_joints = {root, child};
    skeleton->m_rootJoints = {0};
    return skeleton;
}

static std::shared_ptr<AnimationClip> makeSimpleClip()
{
    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = "Walk";

    // Translate root from (0,0,0) to (5,0,0) over 1 second
    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::LINEAR;
    ch.timestamps = {0.0f, 1.0f};
    ch.values = {0, 0, 0,  5, 0, 0};
    clip->m_channels.push_back(ch);

    clip->computeDuration();
    return clip;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(SkeletonAnimatorTest, DefaultState)
{
    SkeletonAnimator animator;
    EXPECT_FALSE(animator.isPlaying());
    EXPECT_FALSE(animator.isPaused());
    EXPECT_TRUE(animator.isLooping());
    EXPECT_FLOAT_EQ(animator.getSpeed(), 1.0f);
    EXPECT_FLOAT_EQ(animator.getCurrentTime(), 0.0f);
    EXPECT_EQ(animator.getActiveClipIndex(), -1);
    EXPECT_FALSE(animator.hasBones());
    EXPECT_TRUE(animator.getBoneMatrices().empty());
}

TEST(SkeletonAnimatorTest, SetSkeleton)
{
    SkeletonAnimator animator;
    auto skeleton = makeSimpleSkeleton();
    animator.setSkeleton(skeleton);

    EXPECT_EQ(animator.getSkeleton(), skeleton);
    EXPECT_EQ(animator.getBoneMatrices().size(), 2u);
}

TEST(SkeletonAnimatorTest, AddAndPlayClip)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());

    EXPECT_EQ(animator.getClipCount(), 1);
    EXPECT_EQ(animator.getClip(0)->getName(), "Walk");

    animator.play("Walk");
    EXPECT_TRUE(animator.isPlaying());
    EXPECT_EQ(animator.getActiveClipIndex(), 0);
    EXPECT_FLOAT_EQ(animator.getCurrentTime(), 0.0f);
}

TEST(SkeletonAnimatorTest, UpdateAdvancesTime)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());
    animator.play("Walk");

    animator.update(0.5f);
    EXPECT_NEAR(animator.getCurrentTime(), 0.5f, 0.001f);
}

TEST(SkeletonAnimatorTest, UpdateComputesBoneMatrices)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());
    animator.play("Walk");

    animator.update(0.5f);

    EXPECT_TRUE(animator.hasBones());
    const auto& matrices = animator.getBoneMatrices();
    EXPECT_EQ(matrices.size(), 2u);

    // Root joint at t=0.5: translated to (2.5, 0, 0)
    // bone matrix = global * inverseBindMatrix (identity for root)
    // So bone[0] should translate by (2.5, 0, 0)
    EXPECT_NEAR(matrices[0][3][0], 2.5f, 0.1f);  // x translation
}

TEST(SkeletonAnimatorTest, LoopingWrapsTime)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());
    animator.setLooping(true);
    animator.play("Walk");

    // Duration is 1.0s, advance by 1.5s should wrap to 0.5s
    animator.update(1.5f);
    EXPECT_NEAR(animator.getCurrentTime(), 0.5f, 0.01f);
}

TEST(SkeletonAnimatorTest, NonLoopingStopsAtEnd)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());
    animator.setLooping(false);
    animator.play("Walk");

    animator.update(2.0f);
    EXPECT_FALSE(animator.isPlaying());
    EXPECT_FLOAT_EQ(animator.getCurrentTime(), 1.0f);
}

TEST(SkeletonAnimatorTest, PauseStopsAdvance)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());
    animator.play("Walk");

    animator.update(0.3f);
    animator.setPaused(true);
    float timeAtPause = animator.getCurrentTime();

    animator.update(0.5f);
    EXPECT_FLOAT_EQ(animator.getCurrentTime(), timeAtPause);
}

TEST(SkeletonAnimatorTest, SpeedMultiplier)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());
    animator.setSpeed(2.0f);
    animator.play("Walk");

    animator.update(0.25f);
    // With speed=2, effective dt = 0.5
    EXPECT_NEAR(animator.getCurrentTime(), 0.5f, 0.01f);
}

TEST(SkeletonAnimatorTest, StopResetsState)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());
    animator.play("Walk");
    animator.update(0.5f);

    animator.stop();
    EXPECT_FALSE(animator.isPlaying());
    EXPECT_FLOAT_EQ(animator.getCurrentTime(), 0.0f);
    EXPECT_EQ(animator.getActiveClipIndex(), -1);
}

TEST(SkeletonAnimatorTest, ClonePreservesState)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());
    animator.setSpeed(1.5f);
    animator.setLooping(false);
    animator.play("Walk");

    auto cloned = animator.clone();
    auto* clonedAnim = dynamic_cast<SkeletonAnimator*>(cloned.get());
    ASSERT_NE(clonedAnim, nullptr);

    // Shared data should be the same
    EXPECT_EQ(clonedAnim->getSkeleton(), animator.getSkeleton());
    EXPECT_EQ(clonedAnim->getClipCount(), 1);

    // Playback state should be reset for cloned entity
    EXPECT_FLOAT_EQ(clonedAnim->getCurrentTime(), 0.0f);
    EXPECT_FLOAT_EQ(clonedAnim->getSpeed(), 1.5f);
    EXPECT_FALSE(clonedAnim->isLooping());
}

TEST(SkeletonAnimatorTest, PlayByIndex)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());

    animator.playIndex(0);
    EXPECT_TRUE(animator.isPlaying());
    EXPECT_EQ(animator.getActiveClipIndex(), 0);

    // Invalid index does nothing
    animator.playIndex(99);
    EXPECT_EQ(animator.getActiveClipIndex(), 0);
}

TEST(SkeletonAnimatorTest, PlayNonExistentClipDoesNothing)
{
    SkeletonAnimator animator;
    animator.setSkeleton(makeSimpleSkeleton());
    animator.addClip(makeSimpleClip());

    animator.play("NonExistent");
    EXPECT_FALSE(animator.isPlaying());
}

// ---------------------------------------------------------------------------
// AUDIT A1 — bone matrices for shuffled-storage skeleton match the matrices
// for the same hierarchy stored in parent-before-child order. Pre-A1 this
// equivalence broke because computeBoneMatrices iterated 0..N and read the
// parent's global before it had been written.
// ---------------------------------------------------------------------------
TEST(SkeletonAnimatorTest, ShuffledStorageEqualsSortedStorageBoneMatrices_A1)
{
    // Hierarchy (logical): Root → Mid → Leaf.
    const glm::mat4 rootLocal = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 midLocal  = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f));
    const glm::mat4 leafLocal = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 rootIbm   = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    const glm::mat4 midIbm    = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 3.0f, 0.0f)));
    const glm::mat4 leafIbm   = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 4.0f, 0.0f)));

    // Sorted storage: parent index < own index everywhere.
    auto sorted = std::make_shared<Skeleton>();
    {
        Joint r; r.name = "Root"; r.parentIndex = -1;
        r.localBindTransform = rootLocal; r.inverseBindMatrix = rootIbm;
        Joint m; m.name = "Mid"; m.parentIndex = 0;
        m.localBindTransform = midLocal; m.inverseBindMatrix = midIbm;
        Joint l; l.name = "Leaf"; l.parentIndex = 1;
        l.localBindTransform = leafLocal; l.inverseBindMatrix = leafIbm;
        sorted->m_joints = {r, m, l};
        sorted->m_rootJoints = {0};
        sorted->buildUpdateOrder();
    }

    // Shuffled storage: same hierarchy with indices [Leaf=0, Root=1, Mid=2].
    auto shuffled = std::make_shared<Skeleton>();
    {
        Joint l; l.name = "Leaf"; l.parentIndex = 2;
        l.localBindTransform = leafLocal; l.inverseBindMatrix = leafIbm;
        Joint r; r.name = "Root"; r.parentIndex = -1;
        r.localBindTransform = rootLocal; r.inverseBindMatrix = rootIbm;
        Joint m; m.name = "Mid"; m.parentIndex = 1;
        m.localBindTransform = midLocal; m.inverseBindMatrix = midIbm;
        shuffled->m_joints = {l, r, m};
        shuffled->m_rootJoints = {1};
        shuffled->buildUpdateOrder();
    }

    // Drive both animators through one tick of a degenerate "play the bind
    // pose" clip so computeBoneMatrices runs and writes m_boneMatrices.
    auto makeClip = [](int rootJointIndex) {
        auto clip = std::make_shared<AnimationClip>();
        clip->m_name = "Bind";
        AnimationChannel ch;
        ch.jointIndex = rootJointIndex;
        ch.targetPath = AnimTargetPath::TRANSLATION;
        ch.interpolation = AnimInterpolation::STEP;
        ch.timestamps = {0.0f, 1.0f};
        ch.values = {1, 0, 0,  1, 0, 0};
        clip->m_channels.push_back(ch);
        clip->computeDuration();
        return clip;
    };

    SkeletonAnimator sortedAnim;
    sortedAnim.setSkeleton(sorted);
    sortedAnim.addClip(makeClip(0));
    sortedAnim.play("Bind");
    sortedAnim.update(0.0f);

    SkeletonAnimator shuffledAnim;
    shuffledAnim.setSkeleton(shuffled);
    shuffledAnim.addClip(makeClip(1));
    shuffledAnim.play("Bind");
    shuffledAnim.update(0.0f);

    const auto& sortedBones = sortedAnim.getBoneMatrices();
    const auto& shuffledBones = shuffledAnim.getBoneMatrices();
    ASSERT_EQ(sortedBones.size(), 3u);
    ASSERT_EQ(shuffledBones.size(), 3u);

    // sortedBones[0]=Root, [1]=Mid, [2]=Leaf.
    // shuffledBones[0]=Leaf, [1]=Root, [2]=Mid.
    auto expectMatNear = [](const glm::mat4& a, const glm::mat4& b, const char* lbl) {
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                EXPECT_NEAR(a[c][r], b[c][r], 1e-4f) << lbl << "[" << c << "][" << r << "]";
            }
        }
    };
    expectMatNear(sortedBones[0], shuffledBones[1], "Root");
    expectMatNear(sortedBones[1], shuffledBones[2], "Mid");
    expectMatNear(sortedBones[2], shuffledBones[0], "Leaf");
}
