// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_morph_target_pipeline.cpp
/// @brief Tests for the GPU morph target deformation pipeline (WEIGHTS sampling, SSBO, weight API).
#include <gtest/gtest.h>

#include "animation/skeleton_animator.h"
#include "animation/animation_clip.h"
#include "animation/skeleton.h"
#include "animation/morph_target.h"
#include "scene/entity.h"
#include "scene/scene.h"

using namespace Vestige;

// --- SkeletonAnimator morph weight API ---

TEST(MorphTargetPipeline, SetMorphTargetCountResizesWeights)
{
    SkeletonAnimator animator;
    EXPECT_TRUE(animator.getMorphWeights().empty());

    animator.setMorphTargetCount(4);
    EXPECT_EQ(animator.getMorphWeights().size(), 4u);
    EXPECT_FLOAT_EQ(animator.getMorphWeights()[0], 0.0f);
    EXPECT_FLOAT_EQ(animator.getMorphWeights()[3], 0.0f);
}

TEST(MorphTargetPipeline, SetMorphWeightByIndex)
{
    SkeletonAnimator animator;
    animator.setMorphTargetCount(3);

    animator.setMorphWeight(0, 1.0f);
    animator.setMorphWeight(1, 0.5f);
    animator.setMorphWeight(2, 0.25f);

    EXPECT_FLOAT_EQ(animator.getMorphWeights()[0], 1.0f);
    EXPECT_FLOAT_EQ(animator.getMorphWeights()[1], 0.5f);
    EXPECT_FLOAT_EQ(animator.getMorphWeights()[2], 0.25f);
}

TEST(MorphTargetPipeline, SetMorphWeightOutOfBoundsIgnored)
{
    SkeletonAnimator animator;
    animator.setMorphTargetCount(2);
    animator.setMorphWeight(5, 1.0f);  // Out of bounds — should not crash
    animator.setMorphWeight(-1, 1.0f); // Negative index — should not crash
    EXPECT_FLOAT_EQ(animator.getMorphWeights()[0], 0.0f);
    EXPECT_FLOAT_EQ(animator.getMorphWeights()[1], 0.0f);
}

TEST(MorphTargetPipeline, NegativeTargetCountIgnored)
{
    SkeletonAnimator animator;
    animator.setMorphTargetCount(-1);
    EXPECT_TRUE(animator.getMorphWeights().empty());
}

// --- WEIGHTS channel sampling ---

static std::shared_ptr<Skeleton> makeMinimalSkeleton()
{
    auto skeleton = std::make_shared<Skeleton>();
    Joint root;
    root.name = "root";
    root.parentIndex = -1;
    root.inverseBindMatrix = glm::mat4(1.0f);
    root.localBindTransform = glm::mat4(1.0f);
    skeleton->m_joints.push_back(root);
    skeleton->m_rootJoints.push_back(0);
    return skeleton;
}

TEST(MorphTargetPipeline, WeightsChannelSampledDuringPlayback)
{
    SkeletonAnimator animator;
    auto skeleton = makeMinimalSkeleton();
    animator.setSkeleton(skeleton);

    // Create a clip with WEIGHTS channel (2 morph targets)
    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = "morph_anim";

    AnimationChannel weightsChannel;
    weightsChannel.jointIndex = -1;  // WEIGHTS channels target mesh, not joint
    weightsChannel.targetPath = AnimTargetPath::WEIGHTS;
    weightsChannel.interpolation = AnimInterpolation::LINEAR;
    weightsChannel.timestamps = {0.0f, 1.0f};
    // 2 morph targets per keyframe: [t0_w0, t0_w1, t1_w0, t1_w1]
    weightsChannel.values = {0.0f, 0.0f, 1.0f, 0.5f};

    clip->m_channels.push_back(weightsChannel);
    clip->computeDuration();

    animator.addClip(clip);
    animator.setLooping(false);
    animator.play("morph_anim");

    // Advance to t=0.5 (midpoint)
    animator.update(0.5f);

    const auto& weights = animator.getMorphWeights();
    ASSERT_GE(weights.size(), 2u);
    // At t=0.5, linearly interpolated: w0 = 0.5, w1 = 0.25
    EXPECT_NEAR(weights[0], 0.5f, 0.05f);
    EXPECT_NEAR(weights[1], 0.25f, 0.05f);
}

TEST(MorphTargetPipeline, WeightsChannelStepInterpolation)
{
    SkeletonAnimator animator;
    auto skeleton = makeMinimalSkeleton();
    animator.setSkeleton(skeleton);

    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = "step_morph";

    AnimationChannel weightsChannel;
    weightsChannel.jointIndex = -1;
    weightsChannel.targetPath = AnimTargetPath::WEIGHTS;
    weightsChannel.interpolation = AnimInterpolation::STEP;
    weightsChannel.timestamps = {0.0f, 0.5f, 1.0f};
    // 1 morph target: values = [0.0, 1.0, 0.0]
    weightsChannel.values = {0.0f, 1.0f, 0.0f};

    clip->m_channels.push_back(weightsChannel);
    clip->computeDuration();

    animator.addClip(clip);
    animator.setLooping(false);
    animator.play("step_morph");

    // Advance to t=0.3 (before second keyframe)
    animator.update(0.3f);

    const auto& weights = animator.getMorphWeights();
    ASSERT_GE(weights.size(), 1u);
    // STEP: still at keyframe 0 value
    EXPECT_NEAR(weights[0], 0.0f, 0.01f);
}

// --- SceneRenderData morph weight propagation ---

TEST(MorphTargetPipeline, RenderDataCarriesMorphWeights)
{
    SceneRenderData::RenderItem item;
    EXPECT_EQ(item.morphWeights, nullptr);
    EXPECT_EQ(item.morphSSBO, 0u);
    EXPECT_EQ(item.morphTargetCount, 0);
    EXPECT_EQ(item.morphVertexCount, 0);

    std::vector<float> weights = {0.5f, 1.0f};
    item.morphWeights = &weights;
    item.morphTargetCount = 2;
    item.morphVertexCount = 100;

    EXPECT_NE(item.morphWeights, nullptr);
    EXPECT_EQ(item.morphWeights->size(), 2u);
    EXPECT_EQ(item.morphTargetCount, 2);
}

// --- MorphTargetData basics ---

TEST(MorphTargetPipeline, EmptyMorphTargetData)
{
    MorphTargetData data;
    EXPECT_TRUE(data.empty());
    EXPECT_EQ(data.targetCount(), 0u);
}

TEST(MorphTargetPipeline, MorphTargetDataWithTargets)
{
    MorphTargetData data;
    data.vertexCount = 10;

    MorphTarget target;
    target.name = "smile";
    target.positionDeltas.resize(10, glm::vec3(0.1f, 0.0f, 0.0f));
    data.targets.push_back(target);
    data.defaultWeights = {0.0f};

    EXPECT_FALSE(data.empty());
    EXPECT_EQ(data.targetCount(), 1u);
    EXPECT_EQ(data.vertexCount, 10u);
}
