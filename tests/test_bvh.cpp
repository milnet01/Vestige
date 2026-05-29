// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_bvh.cpp
/// @brief Unit tests for BVH acceleration structure.
#include "physics/bvh.h"
#include "cloth_test_helpers.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

// Canonical definitions in cloth_test_helpers.h (Slice 18 Ts3, Slice 20 Ts20-DU5).
using Testing::makeTriangle;
using Testing::makeCube;

// ===========================================================================
// BVH — Closest Point on Triangle
// ===========================================================================

TEST(BVHTriangle, PointDirectlyAbove)
{
    glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 0, 1);
    glm::vec3 p(0.2f, 1.0f, 0.2f);
    glm::vec3 closest = BVH::closestPointOnTriangle(p, a, b, c);
    EXPECT_NEAR(closest.x, 0.2f, 1e-5f);
    EXPECT_NEAR(closest.y, 0.0f, 1e-5f);
    EXPECT_NEAR(closest.z, 0.2f, 1e-5f);
}

TEST(BVHTriangle, PointNearVertexA)
{
    glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 0, 1);
    glm::vec3 p(-0.5f, 0.0f, -0.5f);
    glm::vec3 closest = BVH::closestPointOnTriangle(p, a, b, c);
    EXPECT_NEAR(closest.x, 0.0f, 1e-5f);
    EXPECT_NEAR(closest.y, 0.0f, 1e-5f);
    EXPECT_NEAR(closest.z, 0.0f, 1e-5f);
}

TEST(BVHTriangle, PointNearVertexB)
{
    glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 0, 1);
    glm::vec3 p(1.5f, 0.0f, -0.5f);
    glm::vec3 closest = BVH::closestPointOnTriangle(p, a, b, c);
    EXPECT_NEAR(closest.x, 1.0f, 1e-5f);
    EXPECT_NEAR(closest.y, 0.0f, 1e-5f);
    EXPECT_NEAR(closest.z, 0.0f, 1e-5f);
}

TEST(BVHTriangle, PointNearVertexC)
{
    glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 0, 1);
    glm::vec3 p(-0.5f, 0.0f, 1.5f);
    glm::vec3 closest = BVH::closestPointOnTriangle(p, a, b, c);
    EXPECT_NEAR(closest.x, 0.0f, 1e-5f);
    EXPECT_NEAR(closest.y, 0.0f, 1e-5f);
    EXPECT_NEAR(closest.z, 1.0f, 1e-5f);
}

TEST(BVHTriangle, PointNearEdgeAB)
{
    glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 0, 1);
    glm::vec3 p(0.5f, 0.0f, -0.5f);
    glm::vec3 closest = BVH::closestPointOnTriangle(p, a, b, c);
    // Closest should be on edge AB at (0.5, 0, 0)
    EXPECT_NEAR(closest.x, 0.5f, 1e-5f);
    EXPECT_NEAR(closest.y, 0.0f, 1e-5f);
    EXPECT_NEAR(closest.z, 0.0f, 1e-5f);
}

TEST(BVHTriangle, PointNearEdgeBC)
{
    glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 0, 1);
    glm::vec3 p(0.8f, 0.0f, 0.8f);
    glm::vec3 closest = BVH::closestPointOnTriangle(p, a, b, c);
    // Should snap to edge BC (x + z = 1 line)
    EXPECT_NEAR(closest.x + closest.z, 1.0f, 1e-4f);
}

// ===========================================================================
// BVH — Build and Query
// ===========================================================================

TEST(BVHTest, EmptyBVH)
{
    BVH bvh;
    EXPECT_FALSE(bvh.isBuilt());
    EXPECT_EQ(bvh.getNodeCount(), 0u);
    EXPECT_EQ(bvh.getTriangleCount(), 0u);

    BVHQueryResult result;
    EXPECT_FALSE(bvh.queryClosest(glm::vec3(0), 1.0f, nullptr, nullptr, result));
}

TEST(BVHTest, SingleTriangle)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);

    BVH bvh;
    bvh.build(verts.data(), verts.size(), indices.data(), indices.size());

    EXPECT_TRUE(bvh.isBuilt());
    EXPECT_EQ(bvh.getTriangleCount(), 1u);
    EXPECT_GE(bvh.getNodeCount(), 1u);

    // Point above triangle
    BVHQueryResult result;
    bool found = bvh.queryClosest(glm::vec3(0.2f, 0.5f, 0.2f), 1.0f,
                                   verts.data(), indices.data(), result);
    EXPECT_TRUE(found);
    EXPECT_NEAR(result.distance, 0.5f, 1e-4f);
    EXPECT_NEAR(result.closestPoint.y, 0.0f, 1e-5f);
}

TEST(BVHTest, QueryOutOfRange)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);

    BVH bvh;
    bvh.build(verts.data(), verts.size(), indices.data(), indices.size());

    BVHQueryResult result;
    bool found = bvh.queryClosest(glm::vec3(0.2f, 10.0f, 0.2f), 1.0f,
                                   verts.data(), indices.data(), result);
    EXPECT_FALSE(found);
}

TEST(BVHTest, CubeMesh)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeCube(verts, indices);

    BVH bvh;
    bvh.build(verts.data(), verts.size(), indices.data(), indices.size());

    EXPECT_EQ(bvh.getTriangleCount(), 12u);

    // Point outside cube — closest to top face
    BVHQueryResult result;
    bool found = bvh.queryClosest(glm::vec3(0.0f, 1.0f, 0.0f), 2.0f,
                                   verts.data(), indices.data(), result);
    EXPECT_TRUE(found);
    EXPECT_NEAR(result.closestPoint.y, 0.5f, 1e-4f);
    EXPECT_NEAR(result.distance, 0.5f, 1e-4f);
}

TEST(BVHTest, Refit)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);

    BVH bvh;
    bvh.build(verts.data(), verts.size(), indices.data(), indices.size());

    // Move triangle up by 2 units
    for (auto& v : verts) v.y += 2.0f;
    bvh.refit(verts.data(), indices.data());

    // Point at Y=0 should no longer be close (triangle is at Y=2)
    BVHQueryResult result;
    bool found = bvh.queryClosest(glm::vec3(0.2f, 0.0f, 0.2f), 1.0f,
                                   verts.data(), indices.data(), result);
    EXPECT_FALSE(found);

    // Point at Y=2.5 should find the triangle
    found = bvh.queryClosest(glm::vec3(0.2f, 2.5f, 0.2f), 1.0f,
                              verts.data(), indices.data(), result);
    EXPECT_TRUE(found);
    EXPECT_NEAR(result.closestPoint.y, 2.0f, 1e-4f);
}

TEST(BVHTest, NormalFacesQueryPoint)
{
    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);

    BVH bvh;
    bvh.build(verts.data(), verts.size(), indices.data(), indices.size());

    // Point above — normal should point up
    BVHQueryResult result;
    bvh.queryClosest(glm::vec3(0.2f, 1.0f, 0.2f), 2.0f,
                     verts.data(), indices.data(), result);
    EXPECT_GT(result.normal.y, 0.0f);

    // Point below — normal should point down
    bvh.queryClosest(glm::vec3(0.2f, -1.0f, 0.2f), 2.0f,
                     verts.data(), indices.data(), result);
    EXPECT_LT(result.normal.y, 0.0f);
}
