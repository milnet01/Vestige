// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_collision.cpp
/// @brief Unit tests for BVH, SpatialHash, ClothMeshCollider, ColliderGenerator,
///        and ClothSimulator mesh/self-collision integration.
#include "physics/bvh.h"
#include "physics/cloth_mesh_collider.h"
#include "physics/collider_generator.h"
#include "physics/spatial_hash.h"
#include "physics/cloth_simulator.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helpers: simple meshes for testing
// ---------------------------------------------------------------------------

/// A single triangle in the XZ plane at Y=0: (0,0,0), (1,0,0), (0,0,1)
static void makeTriangle(std::vector<glm::vec3>& verts, std::vector<uint32_t>& indices)
{
    verts = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    indices = {0, 1, 2};
}

/// A flat quad (two triangles) in the XZ plane at Y=0: (0,0,0) to (1,0,1)
static void makeQuad(std::vector<glm::vec3>& verts, std::vector<uint32_t>& indices)
{
    verts = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    indices = {0, 1, 2, 0, 2, 3};
}

/// Axis-aligned cube mesh centered at origin, half-extent 0.5
static void makeCube(std::vector<glm::vec3>& verts, std::vector<uint32_t>& indices)
{
    verts = {
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
    };
    indices = {
        // Front
        0, 1, 2,  0, 2, 3,
        // Back
        5, 4, 7,  5, 7, 6,
        // Left
        4, 0, 3,  4, 3, 7,
        // Right
        1, 5, 6,  1, 6, 2,
        // Top
        3, 2, 6,  3, 6, 7,
        // Bottom
        4, 5, 1,  4, 1, 0,
    };
}

static ClothConfig smallClothConfig(uint32_t w = 4, uint32_t h = 4)
{
    ClothConfig cfg;
    cfg.width = w;
    cfg.height = h;
    cfg.spacing = 1.0f;
    cfg.particleMass = 1.0f;
    cfg.substeps = 5;
    cfg.stretchCompliance = 0.0f;
    cfg.shearCompliance = 0.0001f;
    cfg.bendCompliance = 0.01f;
    cfg.damping = 0.01f;
    return cfg;
}

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

// ===========================================================================
// SpatialHash
// ===========================================================================

TEST(SpatialHashTest, EmptyHash)
{
    SpatialHash hash;
    EXPECT_EQ(hash.getEntryCount(), 0u);
}

TEST(SpatialHashTest, BuildAndQuery)
{
    // 4 particles in a line along X
    std::vector<glm::vec3> positions = {
        {0, 0, 0}, {0.5f, 0, 0}, {1.0f, 0, 0}, {5.0f, 0, 0}
    };

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 1.0f);
    EXPECT_EQ(hash.getEntryCount(), 4u);

    // Query around particle 0 with radius 0.6 — should find particle 1 only
    std::vector<uint32_t> result;
    hash.query(positions[0], 0.6f, 0, result);
    EXPECT_EQ(result.size(), 1u);
    if (!result.empty())
    {
        EXPECT_EQ(result[0], 1u);
    }
}

TEST(SpatialHashTest, QueryFindsAllNearby)
{
    std::vector<glm::vec3> positions = {
        {0, 0, 0}, {0.1f, 0, 0}, {0.2f, 0, 0}, {10.0f, 0, 0}
    };

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 0.5f);

    std::vector<uint32_t> result;
    hash.query(positions[0], 0.3f, 0, result);
    EXPECT_EQ(result.size(), 2u);  // particles 1 and 2
}

TEST(SpatialHashTest, SelfExclusion)
{
    std::vector<glm::vec3> positions = {{0, 0, 0}, {0.01f, 0, 0}};

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 1.0f);

    std::vector<uint32_t> result;
    hash.query(positions[0], 1.0f, 0, result);
    // Should find particle 1 but not 0
    EXPECT_EQ(result.size(), 1u);
    if (!result.empty())
    {
        EXPECT_EQ(result[0], 1u);
    }
}

TEST(SpatialHashTest, DistantParticlesNotFound)
{
    std::vector<glm::vec3> positions = {{0, 0, 0}, {100, 0, 0}};

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 1.0f);

    std::vector<uint32_t> result;
    hash.query(positions[0], 1.0f, 0, result);
    EXPECT_TRUE(result.empty());
}

TEST(SpatialHashTest, ThreeDimensionalQuery)
{
    std::vector<glm::vec3> positions = {
        {0, 0, 0},
        {0.3f, 0.3f, 0.3f},  // dist ~0.52
        {0, 1, 0},            // dist 1.0
    };

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 0.6f);

    std::vector<uint32_t> result;
    hash.query(positions[0], 0.6f, 0, result);
    EXPECT_EQ(result.size(), 1u);  // only particle 1
}

TEST(SpatialHashTest, CellSizeStored)
{
    SpatialHash hash;
    std::vector<glm::vec3> positions = {{0, 0, 0}};
    hash.build(positions.data(), positions.size(), 0.42f);
    EXPECT_NEAR(hash.getCellSize(), 0.42f, 1e-6f);
}

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

// ===========================================================================
// ClothSimulator — Mesh Collider Integration
// ===========================================================================

TEST(ClothMeshCollision, MeshColliderPreventsPassthrough)
{
    // Create a cloth that will fall onto a flat triangle mesh at Y=-2
    ClothSimulator sim;
    auto cfg = smallClothConfig(3, 3);
    cfg.spacing = 0.5f;
    cfg.substeps = 10;
    sim.initialize(cfg);

    // Create a large flat quad collider at Y=-2
    std::vector<glm::vec3> quadVerts = {
        {-5, -2, -5}, {5, -2, -5}, {5, -2, 5}, {-5, -2, 5}
    };
    std::vector<uint32_t> quadIdx = {0, 1, 2, 0, 2, 3};

    ClothMeshCollider collider;
    collider.build(quadVerts.data(), quadVerts.size(),
                   quadIdx.data(), quadIdx.size());

    sim.addMeshCollider(&collider);

    // Simulate for 2 seconds (cloth falls under gravity)
    for (int i = 0; i < 120; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    // All particles should be above Y=-2 (collision prevents passthrough)
    const glm::vec3* pos = sim.getPositions();
    for (uint32_t i = 0; i < sim.getParticleCount(); ++i)
    {
        EXPECT_GE(pos[i].y, -2.02f) << "Particle " << i << " fell through mesh collider";
    }
}

TEST(ClothMeshCollision, ClearMeshColliders)
{
    ClothSimulator sim;
    sim.initialize(smallClothConfig(3, 3));

    std::vector<glm::vec3> verts;
    std::vector<uint32_t> indices;
    makeTriangle(verts, indices);
    ClothMeshCollider collider;
    collider.build(verts.data(), verts.size(), indices.data(), indices.size());

    sim.addMeshCollider(&collider);
    sim.clearMeshColliders();

    // Simulating should not crash after clearing
    sim.simulate(1.0f / 60.0f);
}

// ===========================================================================
// ClothSimulator — Self-Collision
// ===========================================================================

TEST(ClothSelfCollision, DefaultDisabled)
{
    ClothSimulator sim;
    sim.initialize(smallClothConfig());
    EXPECT_FALSE(sim.isSelfCollisionEnabled());
    EXPECT_NEAR(sim.getSelfCollisionDistance(), 0.02f, 1e-5f);
}

TEST(ClothSelfCollision, EnableDisable)
{
    ClothSimulator sim;
    sim.initialize(smallClothConfig());

    sim.enableSelfCollision(true);
    EXPECT_TRUE(sim.isSelfCollisionEnabled());

    sim.enableSelfCollision(false);
    EXPECT_FALSE(sim.isSelfCollisionEnabled());
}

TEST(ClothSelfCollision, SetDistance)
{
    ClothSimulator sim;
    sim.initialize(smallClothConfig());

    sim.setSelfCollisionDistance(0.05f);
    EXPECT_NEAR(sim.getSelfCollisionDistance(), 0.05f, 1e-5f);

    // Minimum clamp
    sim.setSelfCollisionDistance(0.0001f);
    EXPECT_GE(sim.getSelfCollisionDistance(), 0.001f);
}

TEST(ClothSelfCollision, SimulateWithSelfCollisionEnabled)
{
    ClothSimulator sim;
    auto cfg = smallClothConfig(4, 4);
    cfg.spacing = 0.5f;
    cfg.substeps = 5;
    sim.initialize(cfg);

    sim.enableSelfCollision(true);
    sim.setSelfCollisionDistance(0.05f);

    // Should not crash when simulating with self-collision
    for (int i = 0; i < 60; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    // Basic sanity: cloth should still be finite
    const glm::vec3* pos = sim.getPositions();
    for (uint32_t i = 0; i < sim.getParticleCount(); ++i)
    {
        EXPECT_FALSE(std::isnan(pos[i].x));
        EXPECT_FALSE(std::isnan(pos[i].y));
        EXPECT_FALSE(std::isnan(pos[i].z));
    }
}

TEST(ClothSelfCollision, FoldedClothMaintainsMinDistance)
{
    // Create a tall narrow cloth that will fold on itself
    ClothSimulator sim;
    auto cfg = smallClothConfig(2, 10);
    cfg.spacing = 0.2f;
    cfg.substeps = 10;
    sim.initialize(cfg);

    // Pin top row
    for (uint32_t c = 0; c < 2; ++c)
    {
        sim.pinParticle(c, sim.getPositions()[c]);
    }

    sim.enableSelfCollision(true);
    sim.setSelfCollisionDistance(0.08f);
    sim.setGroundPlane(-2.0f);

    // Simulate gravity
    for (int i = 0; i < 120; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    // Check that non-adjacent particles maintain minimum distance
    const glm::vec3* pos = sim.getPositions();
    uint32_t count = sim.getParticleCount();
    int violations = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        for (uint32_t j = i + 1; j < count; ++j)
        {
            // Skip adjacent particles (within 1 grid step)
            uint32_t ri = i / 2, ci = i % 2;
            uint32_t rj = j / 2, cj = j % 2;
            int dr = static_cast<int>(ri) - static_cast<int>(rj);
            int dc = static_cast<int>(ci) - static_cast<int>(cj);
            if (dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1)
            {
                continue;
            }

            float dist = glm::length(pos[i] - pos[j]);
            if (dist < 0.04f)  // half the self-collision distance
            {
                violations++;
            }
        }
    }
    // Allow a few minor violations (PBD is approximate)
    EXPECT_LE(violations, 3);
}

// ===========================================================================
// Combined: Mesh Collision + Self-Collision
// ===========================================================================

TEST(ClothCollisionCombined, BothCollisionModesSimultaneously)
{
    ClothSimulator sim;
    auto cfg = smallClothConfig(3, 3);
    cfg.spacing = 0.5f;
    cfg.substeps = 8;
    sim.initialize(cfg);

    // Add mesh collider floor
    std::vector<glm::vec3> floorVerts = {
        {-5, -1.5f, -5}, {5, -1.5f, -5}, {5, -1.5f, 5}, {-5, -1.5f, 5}
    };
    std::vector<uint32_t> floorIdx = {0, 1, 2, 0, 2, 3};
    ClothMeshCollider floor;
    floor.build(floorVerts.data(), floorVerts.size(),
                floorIdx.data(), floorIdx.size());

    sim.addMeshCollider(&floor);
    sim.enableSelfCollision(true);
    sim.setSelfCollisionDistance(0.05f);

    // Simulate
    for (int i = 0; i < 120; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    // Particles should be above floor and not NaN
    const glm::vec3* pos = sim.getPositions();
    for (uint32_t i = 0; i < sim.getParticleCount(); ++i)
    {
        EXPECT_GE(pos[i].y, -1.52f);
        EXPECT_FALSE(std::isnan(pos[i].x));
    }
}
