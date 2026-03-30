/// @file test_morph_target.cpp
/// @brief Unit tests for morph target data structures and CPU blending.
#include "animation/morph_target.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Creates morph target data with 2 targets on 4 vertices.
static MorphTargetData makeSampleData()
{
    MorphTargetData data;
    data.vertexCount = 4;

    // Target 0: moves all vertices +1 in X
    MorphTarget t0;
    t0.name = "moveX";
    t0.positionDeltas = {
        glm::vec3(1, 0, 0), glm::vec3(1, 0, 0),
        glm::vec3(1, 0, 0), glm::vec3(1, 0, 0)
    };
    t0.normalDeltas = {
        glm::vec3(0.1f, 0, 0), glm::vec3(0.1f, 0, 0),
        glm::vec3(0.1f, 0, 0), glm::vec3(0.1f, 0, 0)
    };
    data.targets.push_back(t0);

    // Target 1: moves all vertices +2 in Y
    MorphTarget t1;
    t1.name = "moveY";
    t1.positionDeltas = {
        glm::vec3(0, 2, 0), glm::vec3(0, 2, 0),
        glm::vec3(0, 2, 0), glm::vec3(0, 2, 0)
    };
    // No normal deltas for this target
    data.targets.push_back(t1);

    data.defaultWeights = {0.0f, 0.0f};
    return data;
}

static std::vector<glm::vec3> makeBasePositions()
{
    return {
        glm::vec3(0, 0, 0), glm::vec3(1, 0, 0),
        glm::vec3(0, 1, 0), glm::vec3(1, 1, 0)
    };
}

static std::vector<glm::vec3> makeBaseNormals()
{
    return {
        glm::vec3(0, 0, 1), glm::vec3(0, 0, 1),
        glm::vec3(0, 0, 1), glm::vec3(0, 0, 1)
    };
}

// ---------------------------------------------------------------------------
// Data structure tests
// ---------------------------------------------------------------------------

TEST(MorphTargetTest, EmptyDataHasNoTargets)
{
    MorphTargetData data;
    EXPECT_TRUE(data.empty());
    EXPECT_EQ(data.targetCount(), 0u);
}

TEST(MorphTargetTest, SampleDataHasTwoTargets)
{
    auto data = makeSampleData();
    EXPECT_FALSE(data.empty());
    EXPECT_EQ(data.targetCount(), 2u);
    EXPECT_EQ(data.targets[0].name, "moveX");
    EXPECT_EQ(data.targets[1].name, "moveY");
}

// ---------------------------------------------------------------------------
// Position blending tests
// ---------------------------------------------------------------------------

TEST(MorphTargetTest, ZeroWeightsReturnBasePositions)
{
    auto data = makeSampleData();
    auto base = makeBasePositions();
    std::vector<float> weights = {0.0f, 0.0f};
    std::vector<glm::vec3> out;

    blendMorphPositions(data, weights, base, out);

    ASSERT_EQ(out.size(), base.size());
    for (size_t i = 0; i < base.size(); ++i)
    {
        EXPECT_NEAR(out[i].x, base[i].x, 0.001f);
        EXPECT_NEAR(out[i].y, base[i].y, 0.001f);
        EXPECT_NEAR(out[i].z, base[i].z, 0.001f);
    }
}

TEST(MorphTargetTest, SingleTargetFullWeight)
{
    auto data = makeSampleData();
    auto base = makeBasePositions();
    std::vector<float> weights = {1.0f, 0.0f};  // Only target 0 active
    std::vector<glm::vec3> out;

    blendMorphPositions(data, weights, base, out);

    // Each vertex should be shifted +1 in X
    for (size_t i = 0; i < base.size(); ++i)
    {
        EXPECT_NEAR(out[i].x, base[i].x + 1.0f, 0.001f);
        EXPECT_NEAR(out[i].y, base[i].y, 0.001f);
    }
}

TEST(MorphTargetTest, TwoTargetsCombined)
{
    auto data = makeSampleData();
    auto base = makeBasePositions();
    std::vector<float> weights = {1.0f, 1.0f};  // Both targets at full weight
    std::vector<glm::vec3> out;

    blendMorphPositions(data, weights, base, out);

    // Vertex 0: (0,0,0) + (1,0,0) + (0,2,0) = (1,2,0)
    EXPECT_NEAR(out[0].x, 1.0f, 0.001f);
    EXPECT_NEAR(out[0].y, 2.0f, 0.001f);
    EXPECT_NEAR(out[0].z, 0.0f, 0.001f);
}

TEST(MorphTargetTest, PartialWeight)
{
    auto data = makeSampleData();
    auto base = makeBasePositions();
    std::vector<float> weights = {0.5f, 0.0f};
    std::vector<glm::vec3> out;

    blendMorphPositions(data, weights, base, out);

    // Vertex 0: (0,0,0) + 0.5*(1,0,0) = (0.5, 0, 0)
    EXPECT_NEAR(out[0].x, 0.5f, 0.001f);
}

TEST(MorphTargetTest, WeightAboveOne)
{
    auto data = makeSampleData();
    auto base = makeBasePositions();
    std::vector<float> weights = {2.0f, 0.0f};  // Exaggerated
    std::vector<glm::vec3> out;

    blendMorphPositions(data, weights, base, out);

    // Vertex 0: (0,0,0) + 2.0*(1,0,0) = (2.0, 0, 0)
    EXPECT_NEAR(out[0].x, 2.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Normal blending tests
// ---------------------------------------------------------------------------

TEST(MorphTargetTest, NormalBlendingWithDeltas)
{
    auto data = makeSampleData();
    auto baseNormals = makeBaseNormals();
    std::vector<float> weights = {1.0f, 0.0f};  // Only target 0 has normal deltas
    std::vector<glm::vec3> out;

    blendMorphNormals(data, weights, baseNormals, out);

    // Normal 0: (0,0,1) + (0.1,0,0) = (0.1, 0, 1)
    EXPECT_NEAR(out[0].x, 0.1f, 0.001f);
    EXPECT_NEAR(out[0].z, 1.0f, 0.001f);
}

TEST(MorphTargetTest, NormalBlendingSkipsMissingDeltas)
{
    auto data = makeSampleData();
    auto baseNormals = makeBaseNormals();
    std::vector<float> weights = {0.0f, 1.0f};  // Target 1 has no normal deltas
    std::vector<glm::vec3> out;

    blendMorphNormals(data, weights, baseNormals, out);

    // Should return base normals unchanged
    for (size_t i = 0; i < baseNormals.size(); ++i)
    {
        EXPECT_NEAR(out[i].x, baseNormals[i].x, 0.001f);
        EXPECT_NEAR(out[i].y, baseNormals[i].y, 0.001f);
        EXPECT_NEAR(out[i].z, baseNormals[i].z, 0.001f);
    }
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(MorphTargetTest, EmptyWeightsReturnBase)
{
    auto data = makeSampleData();
    auto base = makeBasePositions();
    std::vector<float> weights;  // Empty
    std::vector<glm::vec3> out;

    blendMorphPositions(data, weights, base, out);

    ASSERT_EQ(out.size(), base.size());
    EXPECT_NEAR(out[0].x, base[0].x, 0.001f);
}

TEST(MorphTargetTest, EmptyDataReturnBase)
{
    MorphTargetData data;
    auto base = makeBasePositions();
    std::vector<float> weights = {1.0f};
    std::vector<glm::vec3> out;

    blendMorphPositions(data, weights, base, out);

    ASSERT_EQ(out.size(), base.size());
    EXPECT_NEAR(out[0].x, base[0].x, 0.001f);
}
