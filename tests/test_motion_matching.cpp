// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_motion_matching.cpp
/// @brief Unit tests for the motion matching system.

#include "experimental/animation/feature_vector.h"
#include "experimental/animation/kd_tree.h"
#include "experimental/animation/motion_database.h"
#include "experimental/animation/trajectory_predictor.h"
#include "experimental/animation/inertialization.h"
#include "experimental/animation/motion_matcher.h"
#include "experimental/animation/motion_preprocessor.h"
#include "experimental/animation/mirror_generator.h"
#include "animation/skeleton.h"
#include "animation/animation_clip.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helper: create a simple 3-joint skeleton (root, leftFoot, rightFoot)
// ---------------------------------------------------------------------------
static std::shared_ptr<Skeleton> createTestSkeleton()
{
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->m_joints.resize(3);

    // Root (hip)
    skeleton->m_joints[0].name = "Hips";
    skeleton->m_joints[0].parentIndex = -1;
    skeleton->m_joints[0].localBindTransform = glm::mat4(1.0f);
    skeleton->m_joints[0].localBindTransform[3] = glm::vec4(0, 1, 0, 1);

    // Left foot
    skeleton->m_joints[1].name = "LeftFoot";
    skeleton->m_joints[1].parentIndex = 0;
    skeleton->m_joints[1].localBindTransform = glm::mat4(1.0f);
    skeleton->m_joints[1].localBindTransform[3] = glm::vec4(-0.2f, -1.0f, 0, 1);

    // Right foot
    skeleton->m_joints[2].name = "RightFoot";
    skeleton->m_joints[2].parentIndex = 0;
    skeleton->m_joints[2].localBindTransform = glm::mat4(1.0f);
    skeleton->m_joints[2].localBindTransform[3] = glm::vec4(0.2f, -1.0f, 0, 1);

    skeleton->m_rootJoints.push_back(0);
    return skeleton;
}

// ---------------------------------------------------------------------------
// Helper: create a simple walking animation clip
// ---------------------------------------------------------------------------
static std::shared_ptr<AnimationClip> createWalkClip(float duration = 2.0f)
{
    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = "Walk";
    clip->m_duration = duration;

    // Root translation channel: moves forward along Z
    AnimationChannel rootTranslation;
    rootTranslation.jointIndex = 0;
    rootTranslation.targetPath = AnimTargetPath::TRANSLATION;
    rootTranslation.interpolation = AnimInterpolation::LINEAR;
    int numKeys = static_cast<int>(duration * 30.0f);
    for (int i = 0; i < numKeys; ++i)
    {
        float t = static_cast<float>(i) / 30.0f;
        rootTranslation.timestamps.push_back(t);
        // X
        rootTranslation.values.push_back(0.0f);
        // Y (slight bounce)
        rootTranslation.values.push_back(1.0f + 0.02f * std::sin(t * 6.28f * 2.0f));
        // Z (forward motion at 1.75 m/s)
        rootTranslation.values.push_back(t * 1.75f);
    }
    clip->m_channels.push_back(std::move(rootTranslation));

    // Left foot channel: oscillates
    AnimationChannel leftFoot;
    leftFoot.jointIndex = 1;
    leftFoot.targetPath = AnimTargetPath::TRANSLATION;
    leftFoot.interpolation = AnimInterpolation::LINEAR;
    for (int i = 0; i < numKeys; ++i)
    {
        float t = static_cast<float>(i) / 30.0f;
        leftFoot.timestamps.push_back(t);
        leftFoot.values.push_back(-0.2f);
        leftFoot.values.push_back(-1.0f + 0.1f * std::max(0.0f, std::sin(t * 6.28f)));
        leftFoot.values.push_back(0.3f * std::sin(t * 6.28f));
    }
    clip->m_channels.push_back(std::move(leftFoot));

    // Right foot channel: opposite phase
    AnimationChannel rightFoot;
    rightFoot.jointIndex = 2;
    rightFoot.targetPath = AnimTargetPath::TRANSLATION;
    rightFoot.interpolation = AnimInterpolation::LINEAR;
    for (int i = 0; i < numKeys; ++i)
    {
        float t = static_cast<float>(i) / 30.0f;
        rightFoot.timestamps.push_back(t);
        rightFoot.values.push_back(0.2f);
        rightFoot.values.push_back(-1.0f + 0.1f * std::max(0.0f, std::sin(t * 6.28f + 3.14f)));
        rightFoot.values.push_back(0.3f * std::sin(t * 6.28f + 3.14f));
    }
    clip->m_channels.push_back(std::move(rightFoot));

    return clip;
}

// ---------------------------------------------------------------------------
// FeatureSchema Tests
// ---------------------------------------------------------------------------

TEST(FeatureSchemaTest, DefaultSchema27Dimensions)
{
    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);
    EXPECT_EQ(schema.getDimensionCount(), 27);
}

TEST(FeatureSchemaTest, CustomSchema)
{
    FeatureSchema schema;
    schema.addBoneFeature({0, true, false, 1.0f}); // Position only = 3
    schema.addBoneFeature({1, false, true, 1.0f}); // Velocity only = 3
    schema.addTrajectorySample({0.5f, 1.0f, 1.0f}); // Pos + dir = 4
    EXPECT_EQ(schema.getDimensionCount(), 10);
}

TEST(FeatureSchemaTest, EmptySchemaZeroDimensions)
{
    FeatureSchema schema;
    EXPECT_EQ(schema.getDimensionCount(), 0);
}

TEST(FeatureSchemaTest, WeightsMatchDimensions)
{
    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);
    auto weights = schema.getWeights();
    EXPECT_EQ(static_cast<int>(weights.size()), schema.getDimensionCount());
}

TEST(FeatureSchemaTest, BoneAndTrajectoryFeatureAccessors)
{
    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);
    EXPECT_EQ(static_cast<int>(schema.getBoneFeatures().size()), 3);
    EXPECT_EQ(static_cast<int>(schema.getTrajectorySamples().size()), 3);
}

// ---------------------------------------------------------------------------
// FeatureExtractor Tests
// ---------------------------------------------------------------------------

TEST(FeatureExtractorTest, ExtractFromPose)
{
    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);

    SkeletonPose pose;
    pose.positions = {
        glm::vec3(0, 1, 0),     // Root/hip
        glm::vec3(-0.2f, 0, 0), // Left foot
        glm::vec3(0.2f, 0, 0)   // Right foot
    };
    pose.velocities = {
        glm::vec3(0, 0, 1.75f), // Hip velocity (forward)
        glm::vec3(0, 0.1f, 0),  // Left foot velocity
        glm::vec3(0, -0.1f, 0)  // Right foot velocity
    };

    glm::vec2 trajPos[3] = {{0, 0.58f}, {0, 1.17f}, {0, 1.75f}};
    glm::vec2 trajDir[3] = {{0, 1}, {0, 1}, {0, 1}};

    std::vector<float> output(27, 0.0f);
    FeatureExtractor::extract(schema, pose, glm::vec3(0, 1, 0), 0.0f,
                              trajPos, trajDir, output.data());

    // Verify output is populated (non-trivial values)
    bool allZero = true;
    for (float v : output)
    {
        if (std::abs(v) > 1e-6f)
        {
            allZero = false;
            break;
        }
    }
    EXPECT_FALSE(allZero);
}

// ---------------------------------------------------------------------------
// FeatureNormalizer Tests
// ---------------------------------------------------------------------------

TEST(FeatureNormalizerTest, ComputeMeanAndStddev)
{
    // 4 frames, 3 features each
    float features[] = {
        1.0f, 2.0f, 3.0f,
        3.0f, 4.0f, 5.0f,
        5.0f, 6.0f, 7.0f,
        7.0f, 8.0f, 9.0f
    };

    FeatureNormalizer normalizer;
    normalizer.compute(features, 4, 3);

    EXPECT_TRUE(normalizer.isReady());
    EXPECT_FLOAT_EQ(normalizer.getMean()[0], 4.0f);
    EXPECT_FLOAT_EQ(normalizer.getMean()[1], 5.0f);
    EXPECT_FLOAT_EQ(normalizer.getMean()[2], 6.0f);
}

TEST(FeatureNormalizerTest, NormalizeAndDenormalize)
{
    float features[] = {1.0f, 5.0f, 2.0f, 6.0f};

    FeatureNormalizer normalizer;
    normalizer.compute(features, 2, 2);

    float query[] = {3.0f, 7.0f};
    float original[] = {3.0f, 7.0f};

    normalizer.normalize(query, 2);
    // After normalization, values should be shifted
    EXPECT_NE(query[0], 3.0f);

    normalizer.denormalize(query, 2);
    // After denormalization, should be back to original
    EXPECT_NEAR(query[0], original[0], 1e-5f);
    EXPECT_NEAR(query[1], original[1], 1e-5f);
}

TEST(FeatureNormalizerTest, ConstantFeatureHandled)
{
    // All values the same — stddev would be 0
    float features[] = {5.0f, 5.0f, 5.0f, 5.0f};

    FeatureNormalizer normalizer;
    normalizer.compute(features, 4, 1);

    // Stddev should be 1.0 (clamped, not zero)
    EXPECT_GT(normalizer.getStddev()[0], 0.0f);
}

// ---------------------------------------------------------------------------
// KDTree Tests
// ---------------------------------------------------------------------------

TEST(KDTreeTest, BuildAndSearch)
{
    // 100 points in 3D
    const int N = 100;
    const int D = 3;
    std::vector<float> data(N * D);
    for (int i = 0; i < N; ++i)
    {
        data[static_cast<size_t>(i * D + 0)] = static_cast<float>(i % 10);
        data[static_cast<size_t>(i * D + 1)] = static_cast<float>(i / 10);
        data[static_cast<size_t>(i * D + 2)] = static_cast<float>(i) * 0.1f;
    }

    KDTree tree;
    tree.build(data.data(), N, D);
    EXPECT_TRUE(tree.isBuilt());
    EXPECT_EQ(tree.getFrameCount(), N);

    // Query for a known point
    float query[] = {3.0f, 4.0f, 4.3f}; // Should be near frame 43
    KDSearchResult result = tree.findNearest(query);
    EXPECT_GE(result.frameIndex, 0);
    EXPECT_LT(result.cost, 1.0f);
}

TEST(KDTreeTest, BruteForceMatchesKDTree)
{
    const int N = 200;
    const int D = 5;
    std::vector<float> data(N * D);
    for (int i = 0; i < N * D; ++i)
    {
        data[static_cast<size_t>(i)] = static_cast<float>(i % 17) * 0.3f;
    }

    KDTree tree;
    tree.build(data.data(), N, D);

    float query[] = {1.2f, 0.9f, 3.6f, 2.1f, 0.3f};

    // Force brute force (small N will already use brute force)
    KDSearchResult bfResult = tree.bruteForceSearch(query);
    KDSearchResult kdResult = tree.findNearest(query);

    EXPECT_EQ(bfResult.frameIndex, kdResult.frameIndex);
    EXPECT_NEAR(bfResult.cost, kdResult.cost, 1e-5f);
}

TEST(KDTreeTest, TagFiltering)
{
    const int N = 50;
    const int D = 2;
    std::vector<float> data(N * D);
    std::vector<uint32_t> tags(N, 0);

    for (int i = 0; i < N; ++i)
    {
        data[static_cast<size_t>(i * D + 0)] = static_cast<float>(i);
        data[static_cast<size_t>(i * D + 1)] = 0.0f;
        tags[static_cast<size_t>(i)] = (i % 2 == 0) ? 0x01 : 0x02;
    }

    KDTree tree;
    tree.build(data.data(), N, D);

    float query[] = {5.0f, 0.0f};

    // Search only tag 0x01 (even frames)
    KDSearchResult result = tree.findNearest(query, 0x01, tags.data());
    EXPECT_GE(result.frameIndex, 0);
    EXPECT_EQ(result.frameIndex % 2, 0); // Should be an even frame
}

TEST(KDTreeTest, EmptyTree)
{
    KDTree tree;
    EXPECT_FALSE(tree.isBuilt());

    float query[] = {1.0f};
    KDSearchResult result = tree.findNearest(query);
    EXPECT_EQ(result.frameIndex, -1);
}

// ---------------------------------------------------------------------------
// TrajectoryPredictor Tests
// ---------------------------------------------------------------------------

TEST(TrajectoryPredictorTest, ZeroInputStaysStill)
{
    TrajectoryPredictor predictor;
    predictor.reset();

    // No input for several frames
    for (int i = 0; i < 60; ++i)
    {
        predictor.update(glm::vec2(0.0f), 0.0f, 0.0f, 1.0f / 60.0f);
    }

    auto vel = predictor.getCurrentVelocity();
    EXPECT_NEAR(vel.x, 0.0f, 0.01f);
    EXPECT_NEAR(vel.y, 0.0f, 0.01f);
}

TEST(TrajectoryPredictorTest, ForwardInputMovesForward)
{
    TrajectoryPredictor predictor;
    predictor.reset();

    // Push forward (positive Y in XZ) at walk speed
    for (int i = 0; i < 120; ++i)
    {
        predictor.update(glm::vec2(0.0f, 1.0f), 1.75f, 0.0f, 1.0f / 60.0f);
    }

    auto vel = predictor.getCurrentVelocity();
    // Should converge toward goal velocity
    EXPECT_GT(glm::length(vel), 1.0f);
}

TEST(TrajectoryPredictorTest, PredictFuturePositions)
{
    TrajectoryPredictor predictor;
    predictor.reset();

    // Steady forward input
    for (int i = 0; i < 60; ++i)
    {
        predictor.update(glm::vec2(0.0f, 1.0f), 2.0f, 0.0f, 1.0f / 60.0f);
    }

    float sampleTimes[] = {0.33f, 0.67f, 1.0f};
    glm::vec2 positions[3];
    glm::vec2 directions[3];
    predictor.predictTrajectory(positions, directions, sampleTimes, 3);

    // Future positions should be ahead of current
    EXPECT_GT(positions[0].y, 0.0f);
    EXPECT_GT(positions[1].y, positions[0].y);
    EXPECT_GT(positions[2].y, positions[1].y);
}

TEST(TrajectoryPredictorTest, SpringConverges)
{
    TrajectoryPredictor predictor;
    predictor.setVelocityHalflife(0.27f);
    predictor.reset();

    // Apply constant input and check convergence
    for (int i = 0; i < 300; ++i)
    {
        predictor.update(glm::vec2(1.0f, 0.0f), 4.0f, 0.0f, 1.0f / 60.0f);
    }

    auto vel = predictor.getCurrentVelocity();
    // Should converge to ~(4.0, 0.0) after 5 seconds at 0.27s halflife
    EXPECT_NEAR(vel.x, 4.0f, 0.1f);
    EXPECT_NEAR(vel.y, 0.0f, 0.1f);
}

// ---------------------------------------------------------------------------
// Inertialization Tests
// ---------------------------------------------------------------------------

TEST(InertializationTest, InitiallyInactive)
{
    Inertialization inert;
    EXPECT_FALSE(inert.isActive());
}

TEST(InertializationTest, StartActivates)
{
    Inertialization inert;

    std::vector<glm::vec3> srcPos = {glm::vec3(1, 0, 0)};
    std::vector<glm::quat> srcRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> srcVel = {glm::vec3(0)};
    std::vector<glm::vec3> dstPos = {glm::vec3(0, 0, 0)};
    std::vector<glm::quat> dstRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> dstVel = {glm::vec3(0)};

    inert.start(srcPos, srcRot, srcVel, dstPos, dstRot, dstVel, 0.1f);
    EXPECT_TRUE(inert.isActive());
}

TEST(InertializationTest, OffsetDecaysToZero)
{
    Inertialization inert;

    std::vector<glm::vec3> srcPos = {glm::vec3(1, 0, 0)};
    std::vector<glm::quat> srcRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> srcVel = {glm::vec3(0)};
    std::vector<glm::vec3> dstPos = {glm::vec3(0, 0, 0)};
    std::vector<glm::quat> dstRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> dstVel = {glm::vec3(0)};

    inert.start(srcPos, srcRot, srcVel, dstPos, dstRot, dstVel, 0.1f);

    // Apply after significant time
    for (int i = 0; i < 100; ++i)
    {
        inert.update(0.01f);
    }

    std::vector<glm::vec3> pos = {glm::vec3(0, 0, 0)};
    std::vector<glm::quat> rot = {glm::quat(1, 0, 0, 0)};
    inert.apply(pos, rot);

    // After 1 second (10× halflife), offset should be negligible
    EXPECT_NEAR(pos[0].x, 0.0f, 0.01f);
}

TEST(InertializationTest, InitialOffsetApplied)
{
    Inertialization inert;

    std::vector<glm::vec3> srcPos = {glm::vec3(2, 0, 0)};
    std::vector<glm::quat> srcRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> srcVel = {glm::vec3(0)};
    std::vector<glm::vec3> dstPos = {glm::vec3(0, 0, 0)};
    std::vector<glm::quat> dstRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> dstVel = {glm::vec3(0)};

    inert.start(srcPos, srcRot, srcVel, dstPos, dstRot, dstVel, 0.1f);

    // At t=0, offset should be full (2, 0, 0)
    std::vector<glm::vec3> pos = {glm::vec3(0, 0, 0)};
    std::vector<glm::quat> rot = {glm::quat(1, 0, 0, 0)};
    inert.apply(pos, rot);

    EXPECT_NEAR(pos[0].x, 2.0f, 0.01f);
}

TEST(InertializationTest, ResetClearsState)
{
    Inertialization inert;

    std::vector<glm::vec3> srcPos = {glm::vec3(1, 0, 0)};
    std::vector<glm::quat> srcRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> srcVel = {glm::vec3(0)};
    std::vector<glm::vec3> dstPos = {glm::vec3(0)};
    std::vector<glm::quat> dstRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> dstVel = {glm::vec3(0)};

    inert.start(srcPos, srcRot, srcVel, dstPos, dstRot, dstVel);
    EXPECT_TRUE(inert.isActive());

    inert.reset();
    EXPECT_FALSE(inert.isActive());
}

TEST(InertializationTest, AxisAnglePreservesPiRotation_A5)
{
    // 180° rotation around X — w == 0, sinHalf == 1; angle should be π and axis (1,0,0).
    Inertialization inert;
    const glm::quat piRotX = glm::angleAxis(glm::pi<float>(), glm::vec3(1, 0, 0));

    std::vector<glm::vec3> srcPos = {glm::vec3(0)};
    std::vector<glm::quat> srcRot = {piRotX};
    std::vector<glm::vec3> srcVel = {glm::vec3(0)};
    std::vector<glm::vec3> dstPos = {glm::vec3(0)};
    std::vector<glm::quat> dstRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> dstVel = {glm::vec3(0)};

    inert.start(srcPos, srcRot, srcVel, dstPos, dstRot, dstVel, 0.1f);

    std::vector<glm::vec3> pos = {glm::vec3(0)};
    std::vector<glm::quat> rot = {glm::quat(1, 0, 0, 0)};
    inert.apply(pos, rot);

    // At t=0 the offset is full; applying it to dst (identity) should give piRotX.
    EXPECT_NEAR(std::abs(rot[0].x), 1.0f, 1e-3f);
    EXPECT_NEAR(rot[0].w, 0.0f, 1e-3f);
}

TEST(InertializationTest, AxisAngleStableNearIdentity_A5)
{
    // 0.001 rad rotation — w very close to 1, the case where 2*acos(w) loses
    // precision because acos's slope diverges at ±1. With sqrt(1-w²)+atan2 the
    // small angle survives.
    Inertialization inert;
    const float tinyAngle = 0.001f;
    const glm::quat tinyRot = glm::angleAxis(tinyAngle, glm::vec3(0, 1, 0));

    std::vector<glm::vec3> srcPos = {glm::vec3(0)};
    std::vector<glm::quat> srcRot = {tinyRot};
    std::vector<glm::vec3> srcVel = {glm::vec3(0)};
    std::vector<glm::vec3> dstPos = {glm::vec3(0)};
    std::vector<glm::quat> dstRot = {glm::quat(1, 0, 0, 0)};
    std::vector<glm::vec3> dstVel = {glm::vec3(0)};

    inert.start(srcPos, srcRot, srcVel, dstPos, dstRot, dstVel, 0.1f);

    std::vector<glm::vec3> pos = {glm::vec3(0)};
    std::vector<glm::quat> rot = {glm::quat(1, 0, 0, 0)};
    inert.apply(pos, rot);

    // Recovered rotation must match input within float epsilon scaled by the angle.
    const glm::quat recovered = rot[0];
    EXPECT_NEAR(recovered.y, tinyRot.y, 1e-5f);
    EXPECT_NEAR(recovered.w, tinyRot.w, 1e-5f);
}

TEST(InertializationTest, AxisAngleZeroForIdenticalRotations_A5)
{
    // Identical src and dst — diff is identity, no rotation offset should accumulate.
    Inertialization inert;
    const glm::quat r = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1, 1, 0)));

    std::vector<glm::vec3> srcPos = {glm::vec3(0)};
    std::vector<glm::quat> srcRot = {r};
    std::vector<glm::vec3> srcVel = {glm::vec3(0)};
    std::vector<glm::vec3> dstPos = {glm::vec3(0)};
    std::vector<glm::quat> dstRot = {r};
    std::vector<glm::vec3> dstVel = {glm::vec3(0)};

    inert.start(srcPos, srcRot, srcVel, dstPos, dstRot, dstVel, 0.1f);

    std::vector<glm::vec3> pos = {glm::vec3(0)};
    std::vector<glm::quat> rot = {r};
    inert.apply(pos, rot);

    // No offset → output is dst unchanged.
    EXPECT_NEAR(rot[0].x, r.x, 1e-6f);
    EXPECT_NEAR(rot[0].y, r.y, 1e-6f);
    EXPECT_NEAR(rot[0].z, r.z, 1e-6f);
    EXPECT_NEAR(rot[0].w, r.w, 1e-6f);
}

// ---------------------------------------------------------------------------
// MotionDatabase Tests
// ---------------------------------------------------------------------------

TEST(MotionDatabaseTest, BuildFromClip)
{
    auto skeleton = createTestSkeleton();
    auto clip = createWalkClip(1.0f);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);

    std::vector<AnimClipEntry> clips;
    AnimClipEntry entry;
    entry.clip = clip;
    entry.defaultTags = MotionTags::LOCOMOTION;
    clips.push_back(entry);

    MotionDatabase db;
    db.build(schema, clips, *skeleton, 30.0f);

    EXPECT_TRUE(db.isBuilt());
    EXPECT_GT(db.getFrameCount(), 0);
    EXPECT_EQ(db.getFeatureCount(), 27);
}

TEST(MotionDatabaseTest, SearchReturnsValidFrame)
{
    auto skeleton = createTestSkeleton();
    auto clip = createWalkClip(2.0f);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);

    std::vector<AnimClipEntry> clips;
    AnimClipEntry entry;
    entry.clip = clip;
    clips.push_back(entry);

    MotionDatabase db;
    db.build(schema, clips, *skeleton, 30.0f);

    // Search with a zero query
    std::vector<float> query(27, 0.0f);
    auto result = db.search(query.data());

    EXPECT_GE(result.frameIndex, 0);
    EXPECT_EQ(result.clipIndex, 0);
    EXPECT_GE(result.clipTime, 0.0f);
}

TEST(MotionDatabaseTest, FrameInfoConsistency)
{
    auto skeleton = createTestSkeleton();
    auto clip = createWalkClip(1.0f);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);

    std::vector<AnimClipEntry> clips;
    AnimClipEntry entry;
    entry.clip = clip;
    entry.defaultTags = MotionTags::LOCOMOTION;
    clips.push_back(entry);

    MotionDatabase db;
    db.build(schema, clips, *skeleton, 30.0f);

    for (int i = 0; i < db.getFrameCount(); ++i)
    {
        const auto& info = db.getFrameInfo(i);
        EXPECT_EQ(info.clipIndex, 0);
        EXPECT_GE(info.clipTime, 0.0f);
        EXPECT_EQ(info.tags, MotionTags::LOCOMOTION);
    }
}

TEST(MotionDatabaseTest, PoseDataAvailable)
{
    auto skeleton = createTestSkeleton();
    auto clip = createWalkClip(0.5f);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);

    std::vector<AnimClipEntry> clips;
    AnimClipEntry entry;
    entry.clip = clip;
    clips.push_back(entry);

    MotionDatabase db;
    db.build(schema, clips, *skeleton, 30.0f);

    EXPECT_TRUE(db.isBuilt());
    const auto& pose = db.getPose(0);
    EXPECT_EQ(static_cast<int>(pose.positions.size()), 3);
}

TEST(MotionDatabaseTest, MirroringDoublesFrames)
{
    auto skeleton = createTestSkeleton();
    auto clip = createWalkClip(1.0f);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);

    std::vector<AnimClipEntry> clips;
    AnimClipEntry entry;
    entry.clip = clip;
    clips.push_back(entry);

    MotionDatabase db;
    db.build(schema, clips, *skeleton, 30.0f);

    int originalCount = db.getFrameCount();
    EXPECT_GT(originalCount, 0);

    std::vector<std::pair<int, int>> pairs = {{1, 2}}; // Left/right foot
    db.addMirroredFrames(pairs);

    EXPECT_EQ(db.getFrameCount(), originalCount * 2);
}

// ---------------------------------------------------------------------------
// MirrorGenerator Tests
// ---------------------------------------------------------------------------

TEST(MirrorGeneratorTest, AutoDetectLeftRight)
{
    auto skeleton = createTestSkeleton();
    MirrorConfig config = MirrorGenerator::autoDetect(*skeleton);

    // Should detect LeftFoot <-> RightFoot
    EXPECT_EQ(static_cast<int>(config.bonePairs.size()), 1);
    EXPECT_EQ(config.bonePairs[0].first, 1);
    EXPECT_EQ(config.bonePairs[0].second, 2);
}

TEST(MirrorGeneratorTest, FromNames)
{
    auto skeleton = createTestSkeleton();
    std::vector<std::pair<std::string, std::string>> pairs = {
        {"LeftFoot", "RightFoot"}
    };

    MirrorConfig config = MirrorGenerator::fromNames(*skeleton, pairs);
    EXPECT_EQ(static_cast<int>(config.bonePairs.size()), 1);
}

TEST(MirrorGeneratorTest, NoMirrorForCenterBones)
{
    Skeleton skeleton;
    skeleton.m_joints.resize(1);
    skeleton.m_joints[0].name = "Spine";
    skeleton.m_joints[0].parentIndex = -1;

    MirrorConfig config = MirrorGenerator::autoDetect(skeleton);
    EXPECT_TRUE(config.bonePairs.empty());
}

// ---------------------------------------------------------------------------
// MotionPreprocessor Tests
// ---------------------------------------------------------------------------

TEST(MotionPreprocessorTest, BuildProducesDatabase)
{
    auto skeleton = createTestSkeleton();
    auto clip = createWalkClip(1.0f);

    MotionPreprocessor preprocessor;
    preprocessor.addClip(clip, MotionTags::LOCOMOTION);
    EXPECT_EQ(preprocessor.getClipCount(), 1);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);
    MotionPreprocessConfig config;
    config.sampleRate = 30.0f;

    auto db = preprocessor.build(schema, *skeleton, config);
    ASSERT_NE(db, nullptr);
    EXPECT_TRUE(db->isBuilt());
    EXPECT_GT(db->getFrameCount(), 0);
}

TEST(MotionPreprocessorTest, WithMirroring)
{
    auto skeleton = createTestSkeleton();
    auto clip = createWalkClip(1.0f);

    MotionPreprocessor preprocessor;
    preprocessor.addClip(clip);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);
    MotionPreprocessConfig config;
    config.sampleRate = 30.0f;
    config.enableMirroring = true;
    config.mirrorBonePairs = {{1, 2}};

    auto db = preprocessor.build(schema, *skeleton, config);
    ASSERT_NE(db, nullptr);

    // Should have double the frames
    MotionPreprocessConfig noMirror;
    noMirror.sampleRate = 30.0f;
    MotionPreprocessor pp2;
    pp2.addClip(clip);
    auto dbNoMirror = pp2.build(schema, *skeleton, noMirror);

    EXPECT_EQ(db->getFrameCount(), dbNoMirror->getFrameCount() * 2);
}

TEST(MotionPreprocessorTest, ClearRemovesClips)
{
    MotionPreprocessor preprocessor;
    preprocessor.addClip(createWalkClip());
    preprocessor.addClip(createWalkClip());
    EXPECT_EQ(preprocessor.getClipCount(), 2);

    preprocessor.clear();
    EXPECT_EQ(preprocessor.getClipCount(), 0);
}

// ---------------------------------------------------------------------------
// MotionMatcher Tests
// ---------------------------------------------------------------------------

TEST(MotionMatcherTest, InactiveWithoutDatabase)
{
    MotionMatcher matcher;
    EXPECT_FALSE(matcher.isActive());
}

TEST(MotionMatcherTest, TuningParameters)
{
    MotionMatcher matcher;

    matcher.setSearchInterval(0.05f);
    matcher.setTransitionCost(0.01f);
    matcher.setInertializationHalflife(0.15f);
    matcher.setTrajectoryHalflife(0.3f);
    matcher.setTagMask(MotionTags::LOCOMOTION);

    // Should not crash
    EXPECT_FALSE(matcher.isActive());
}

TEST(MotionMatcherTest, DebugQueryDefaults)
{
    MotionMatcher matcher;
    EXPECT_FLOAT_EQ(matcher.getLastSearchCost(), 0.0f);
    EXPECT_EQ(matcher.getLastMatchFrame(), -1);
    EXPECT_EQ(matcher.getLastMatchClip(), -1);
    EXPECT_EQ(matcher.getFramesSinceTransition(), 0);
}

TEST(MotionMatcherTest, TrajectoryPredictorAccessible)
{
    MotionMatcher matcher;
    auto& predictor = matcher.getTrajectoryPredictor();
    predictor.setVelocityHalflife(0.5f);
    // Should not crash
    auto vel = predictor.getCurrentVelocity();
    EXPECT_FLOAT_EQ(vel.x, 0.0f);
}

// ---------------------------------------------------------------------------
// MotionTags Tests
// ---------------------------------------------------------------------------

TEST(MotionTagsTest, TagValues)
{
    EXPECT_EQ(MotionTags::LOCOMOTION, 0x01u);
    EXPECT_EQ(MotionTags::IDLE, 0x02u);
    EXPECT_EQ(MotionTags::TURNING, 0x04u);
    EXPECT_EQ(MotionTags::NO_ENTRY, 0x80u);
}

TEST(MotionTagsTest, TagCombination)
{
    uint32_t tags = MotionTags::LOCOMOTION | MotionTags::TURNING;
    EXPECT_TRUE((tags & MotionTags::LOCOMOTION) != 0);
    EXPECT_TRUE((tags & MotionTags::TURNING) != 0);
    EXPECT_FALSE((tags & MotionTags::IDLE) != 0);
}

// ---------------------------------------------------------------------------
// Integration Tests
// ---------------------------------------------------------------------------

TEST(MotionMatchingIntegration, EndToEndDatabaseBuildAndSearch)
{
    auto skeleton = createTestSkeleton();
    auto walkClip = createWalkClip(2.0f);

    // Build database through preprocessor
    MotionPreprocessor preprocessor;
    preprocessor.addClip(walkClip, MotionTags::LOCOMOTION);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);
    MotionPreprocessConfig config;
    config.sampleRate = 30.0f;
    config.enableMirroring = true;
    config.mirrorBonePairs = {{1, 2}};

    auto db = preprocessor.build(schema, *skeleton, config);
    ASSERT_NE(db, nullptr);
    EXPECT_TRUE(db->isBuilt());

    // Search for a walking pose
    std::vector<float> query(27, 0.0f);
    auto result = db->search(query.data(), MotionTags::LOCOMOTION);
    EXPECT_GE(result.frameIndex, 0);
    EXPECT_EQ(result.clipIndex, 0);

    // Search with no matching tag should still return a result (all frames match)
    auto result2 = db->search(query.data());
    EXPECT_GE(result2.frameIndex, 0);
}

TEST(MotionMatchingIntegration, TrajectoryToDatabaseSearch)
{
    auto skeleton = createTestSkeleton();
    auto clip = createWalkClip(2.0f);

    FeatureSchema schema = FeatureSchema::createDefault(1, 2, 0);
    std::vector<AnimClipEntry> clips;
    AnimClipEntry entry;
    entry.clip = clip;
    clips.push_back(entry);

    MotionDatabase db;
    db.build(schema, clips, *skeleton, 30.0f);

    // Simulate trajectory prediction
    TrajectoryPredictor predictor;
    predictor.reset();
    for (int i = 0; i < 60; ++i)
    {
        predictor.update(glm::vec2(0, 1), 1.75f, 0.0f, 1.0f / 60.0f);
    }

    float sampleTimes[] = {0.33f, 0.67f, 1.0f};
    glm::vec2 trajPos[3], trajDir[3];
    predictor.predictTrajectory(trajPos, trajDir, sampleTimes, 3);

    // Build query from a default pose + predicted trajectory
    SkeletonPose pose;
    pose.positions = {glm::vec3(0, 1, 0), glm::vec3(-0.2f, 0, 0), glm::vec3(0.2f, 0, 0)};
    pose.velocities = {glm::vec3(0, 0, 1.75f), glm::vec3(0), glm::vec3(0)};

    std::vector<float> query(27, 0.0f);
    FeatureExtractor::extract(schema, pose, glm::vec3(0, 1, 0), 0.0f,
                              trajPos, trajDir, query.data());

    auto result = db.search(query.data());
    EXPECT_GE(result.frameIndex, 0);
    EXPECT_LT(result.cost, 1e10f);
}
