// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_mesh_collider.cpp
/// @brief Unit tests for ClothMeshCollider and ColliderGenerator.
#include "physics/cloth_mesh_collider.h"
#include "physics/collider_generator.h"
#include "cloth_test_helpers.h"

#include <gtest/gtest.h>

using namespace Vestige;

// Canonical definitions in cloth_test_helpers.h (Slice 18 Ts3, Slice 20 Ts20-DU5).
using Testing::makeTriangle;
using Testing::makeQuad;
using Testing::makeCube;

// ===========================================================================
// ClothMeshCollider
// ===========================================================================

TEST(ClothMeshColliderTest, BuildFromTriangle)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);

    ClothMeshCollider collider;
    collider.build(verts.data(), verts.size(), indices.data(), indices.size());

    EXPECT_TRUE(collider.isBuilt());
    EXPECT_EQ(collider.getTriangleCount(), 1u);
    EXPECT_EQ(collider.getVertexCount(), 3u);
}

TEST(ClothMeshColliderTest, QueryClosestPoint)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);

    ClothMeshCollider collider;
    collider.build(verts.data(), verts.size(), indices.data(), indices.size());

    glm::vec3 outPt, outNormal;
    float outDist = 0.0f;
    bool found = collider.queryClosest(glm::vec3(0.2f, 0.3f, 0.2f), 1.0f,
                                        outPt, outNormal, outDist);
    EXPECT_TRUE(found);
    EXPECT_NEAR(outDist, 0.3f, 1e-4f);
    EXPECT_NEAR(outPt.y, 0.0f, 1e-5f);
}

TEST(ClothMeshColliderTest, QueryOutOfRange)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);

    ClothMeshCollider collider;
    collider.build(verts.data(), verts.size(), indices.data(), indices.size());

    glm::vec3 outPt, outNormal;
    float outDist = 0.0f;
    bool found = collider.queryClosest(glm::vec3(0.2f, 10.0f, 0.2f), 1.0f,
                                        outPt, outNormal, outDist);
    EXPECT_FALSE(found);
}

TEST(ClothMeshColliderTest, UpdateVertices)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);

    ClothMeshCollider collider;
    collider.build(verts.data(), verts.size(), indices.data(), indices.size());

    // Move triangle up by 3
    for (auto& v : verts) v.y += 3.0f;
    collider.updateVertices(verts.data(), verts.size());

    // Old position should miss
    glm::vec3 outPt, outNormal;
    float outDist = 0.0f;
    bool found = collider.queryClosest(glm::vec3(0.2f, 0.3f, 0.2f), 1.0f,
                                        outPt, outNormal, outDist);
    EXPECT_FALSE(found);

    // New position should hit
    found = collider.queryClosest(glm::vec3(0.2f, 3.3f, 0.2f), 1.0f,
                                   outPt, outNormal, outDist);
    EXPECT_TRUE(found);
    EXPECT_NEAR(outPt.y, 3.0f, 1e-4f);
}

TEST(ClothMeshColliderTest, CubeMeshQuery)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeCube(verts, indices);

    ClothMeshCollider collider;
    collider.build(verts.data(), verts.size(), indices.data(), indices.size());

    EXPECT_EQ(collider.getTriangleCount(), 12u);

    // Point above cube top face
    glm::vec3 outPt, outNormal;
    float outDist = 0.0f;
    bool found = collider.queryClosest(glm::vec3(0.0f, 0.6f, 0.0f), 0.2f,
                                        outPt, outNormal, outDist);
    EXPECT_TRUE(found);
    EXPECT_NEAR(outPt.y, 0.5f, 1e-3f);
    EXPECT_NEAR(outDist, 0.1f, 1e-3f);
}

TEST(ClothMeshColliderTest, NotBuiltReturnsEmpty)
{
    ClothMeshCollider collider;
    EXPECT_FALSE(collider.isBuilt());
    EXPECT_EQ(collider.getTriangleCount(), 0u);
    EXPECT_EQ(collider.getVertexCount(), 0u);

    glm::vec3 outPt, outNormal;
    float outDist = 0.0f;
    EXPECT_FALSE(collider.queryClosest(glm::vec3(0), 1.0f, outPt, outNormal, outDist));
}

// ===========================================================================
// ColliderGenerator
// ===========================================================================

TEST(ColliderGeneratorTest, FromMesh)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeQuad(verts, indices);

    ClothMeshCollider collider = ColliderGenerator::fromMesh(
        verts.data(), verts.size(), indices.data(), indices.size());

    EXPECT_TRUE(collider.isBuilt());
    EXPECT_EQ(collider.getTriangleCount(), 2u);
    EXPECT_EQ(collider.getVertexCount(), 4u);

    // Query should work
    glm::vec3 outPt, outNormal;
    float outDist = 0.0f;
    bool found = collider.queryClosest(glm::vec3(0.5f, 0.1f, 0.5f), 0.2f,
                                        outPt, outNormal, outDist);
    EXPECT_TRUE(found);
    EXPECT_NEAR(outDist, 0.1f, 1e-4f);
}
