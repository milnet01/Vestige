// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gltf_loader.cpp
/// @brief Tests for ModelNode transform math and Model data structure.
#include <gtest/gtest.h>

#include "resource/model.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace Vestige;

/// @brief Helper to compare matrices with tolerance.
static void expectMatrixNear(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f)
{
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 4; row++)
        {
            EXPECT_NEAR(a[col][row], b[col][row], eps)
                << "Mismatch at [" << col << "][" << row << "]";
        }
    }
}

// =============================================================================
// ModelNodeTest — Transform math
// =============================================================================

TEST(ModelNodeTest, IdentityTrsProducesIdentityMatrix)
{
    ModelNode node;
    glm::mat4 result = node.computeLocalMatrix();
    expectMatrixNear(result, glm::mat4(1.0f));
}

TEST(ModelNodeTest, TranslationProducesCorrectMatrix)
{
    ModelNode node;
    node.translation = glm::vec3(3.0f, -1.0f, 7.0f);

    glm::mat4 result = node.computeLocalMatrix();
    glm::mat4 expected = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -1.0f, 7.0f));

    expectMatrixNear(result, expected);

    // Translation is in column 3
    EXPECT_FLOAT_EQ(result[3][0], 3.0f);
    EXPECT_FLOAT_EQ(result[3][1], -1.0f);
    EXPECT_FLOAT_EQ(result[3][2], 7.0f);
}

TEST(ModelNodeTest, Rotation90DegreesAroundY)
{
    ModelNode node;
    // 90 degrees around Y axis
    float angle = glm::radians(90.0f);
    node.rotation = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 result = node.computeLocalMatrix();

    // Rotating X axis (1,0,0) by 90° around Y should give (0,0,-1)
    glm::vec4 xAxis = result * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(xAxis.x, 0.0f, 1e-5f);
    EXPECT_NEAR(xAxis.y, 0.0f, 1e-5f);
    EXPECT_NEAR(xAxis.z, -1.0f, 1e-5f);
}

TEST(ModelNodeTest, ScaleProducesCorrectDiagonal)
{
    ModelNode node;
    node.scale = glm::vec3(2.0f, 3.0f, 0.5f);

    glm::mat4 result = node.computeLocalMatrix();

    EXPECT_FLOAT_EQ(result[0][0], 2.0f);
    EXPECT_FLOAT_EQ(result[1][1], 3.0f);
    EXPECT_FLOAT_EQ(result[2][2], 0.5f);
    EXPECT_FLOAT_EQ(result[3][3], 1.0f);
}

TEST(ModelNodeTest, DirectMatrixModeReturnsMatrixUnchanged)
{
    ModelNode node;
    glm::mat4 custom = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 20.0f, 30.0f));
    custom = glm::scale(custom, glm::vec3(2.0f));

    node.hasMatrix = true;
    node.matrix = custom;

    // TRS should be ignored
    node.translation = glm::vec3(99.0f, 99.0f, 99.0f);

    glm::mat4 result = node.computeLocalMatrix();
    expectMatrixNear(result, custom);
}

TEST(ModelNodeTest, ComputeLocalMatrixComposesTRS)
{
    ModelNode node;
    node.translation = glm::vec3(1.0f, 2.0f, 3.0f);
    float angle = glm::radians(45.0f);
    node.rotation = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f));
    node.scale = glm::vec3(2.0f, 2.0f, 2.0f);

    glm::mat4 result = node.computeLocalMatrix();

    // Expected: T * R * S
    glm::mat4 t = glm::translate(glm::mat4(1.0f), node.translation);
    glm::mat4 r = glm::mat4_cast(node.rotation);
    glm::mat4 s = glm::scale(glm::mat4(1.0f), node.scale);
    glm::mat4 expected = t * r * s;

    expectMatrixNear(result, expected);
}

// =============================================================================
// ModelTest — Model data structure
// =============================================================================

TEST(ModelTest, EmptyModelHasZeroCounts)
{
    Model model;
    EXPECT_EQ(model.getMeshCount(), 0u);
    EXPECT_EQ(model.getMaterialCount(), 0u);
    EXPECT_EQ(model.getTextureCount(), 0u);
    EXPECT_EQ(model.getNodeCount(), 0u);
}

TEST(ModelTest, CountsMatchAddedData)
{
    Model model;

    // Add some primitives (mesh is nullptr — that's fine for counting)
    model.m_primitives.push_back({nullptr, 0, AABB{}});
    model.m_primitives.push_back({nullptr, 1, AABB{}});
    model.m_primitives.push_back({nullptr, 0, AABB{}});

    // Add materials
    model.m_materials.push_back(std::make_shared<Material>());
    model.m_materials.push_back(std::make_shared<Material>());

    // Add textures
    model.m_textures.push_back(std::make_shared<Texture>());

    // Add nodes
    model.m_nodes.push_back(ModelNode{});
    model.m_nodes.push_back(ModelNode{});
    model.m_nodes.push_back(ModelNode{});
    model.m_nodes.push_back(ModelNode{});

    EXPECT_EQ(model.getMeshCount(), 3u);
    EXPECT_EQ(model.getMaterialCount(), 2u);
    EXPECT_EQ(model.getTextureCount(), 1u);
    EXPECT_EQ(model.getNodeCount(), 4u);
}

TEST(ModelTest, BoundsComputedFromPrimitives)
{
    Model model;

    ModelPrimitive prim1;
    prim1.bounds = {glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 2.0f, 1.0f)};
    model.m_primitives.push_back(prim1);

    ModelPrimitive prim2;
    prim2.bounds = {glm::vec3(0.0f, -3.0f, 0.0f), glm::vec3(5.0f, 1.0f, 4.0f)};
    model.m_primitives.push_back(prim2);

    AABB bounds = model.getBounds();
    EXPECT_FLOAT_EQ(bounds.min.x, -1.0f);
    EXPECT_FLOAT_EQ(bounds.min.y, -3.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, -1.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 5.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 2.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, 4.0f);
}
