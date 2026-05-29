// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_simulation_collision.cpp
/// @brief Unit tests for ClothSimulator mesh collision and self-collision.
#include "physics/cloth_simulator.h"
#include "physics/cloth_mesh_collider.h"
#include "cloth_test_helpers.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

// Canonical definitions in cloth_test_helpers.h (Slice 18 Ts3, Slice 20 Ts20-DU5).
using Testing::clothSmallConfig;
using Testing::makeTriangle;

// ===========================================================================
// ClothSimulator — Mesh Collider Integration
// ===========================================================================

TEST(ClothMeshCollision, MeshColliderPreventsPassthrough)
{
    // Create a cloth that will fall onto a flat triangle mesh at Y=-2
    ClothSimulator sim;
    auto cfg = clothSmallConfig(3, 3);
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
    sim.initialize(clothSmallConfig(3, 3));

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
    sim.initialize(clothSmallConfig());
    EXPECT_FALSE(sim.isSelfCollisionEnabled());
    EXPECT_NEAR(sim.getSelfCollisionDistance(), 0.02f, 1e-5f);
}

TEST(ClothSelfCollision, EnableDisable)
{
    ClothSimulator sim;
    sim.initialize(clothSmallConfig());

    sim.enableSelfCollision(true);
    EXPECT_TRUE(sim.isSelfCollisionEnabled());

    sim.enableSelfCollision(false);
    EXPECT_FALSE(sim.isSelfCollisionEnabled());
}

TEST(ClothSelfCollision, SetDistance)
{
    ClothSimulator sim;
    sim.initialize(clothSmallConfig());

    sim.setSelfCollisionDistance(0.05f);
    EXPECT_NEAR(sim.getSelfCollisionDistance(), 0.05f, 1e-5f);

    // Minimum clamp
    sim.setSelfCollisionDistance(0.0001f);
    EXPECT_GE(sim.getSelfCollisionDistance(), 0.001f);
}

TEST(ClothSelfCollision, SimulateWithSelfCollisionEnabled)
{
    ClothSimulator sim;
    auto cfg = clothSmallConfig(4, 4);
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
    auto cfg = clothSmallConfig(2, 10);
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
    auto cfg = clothSmallConfig(3, 3);
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
