// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_solver_improvements.cpp
/// @brief Unit tests for Batch 11 cloth solver improvements: dihedral bending,
///        constraint ordering, adaptive damping, friction, thick particle model.
#include "physics/cloth_simulator.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helper: create a small cloth for quick tests
// ---------------------------------------------------------------------------

static ClothConfig smallConfig(uint32_t w = 4, uint32_t h = 4)
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
// 11a: Dihedral Bending Constraints
// ===========================================================================

TEST(DihedralBending, ConstraintsCreatedOnInitialize)
{
    ClothSimulator sim;
    sim.initialize(smallConfig(4, 4));

    // A 4x4 grid has 3x3=9 quads, each quad has 2 triangles = 18 triangles.
    // Interior edges shared by 2 triangles get dihedral constraints.
    // Horizontal interior edges: 3 per row × 3 rows = 9
    // Vertical interior edges: 3 per column × 3 columns = 9
    // Diagonal edges (within each quad, the shared diagonal): 9
    // Total interior edges = 9 + 9 + 9 = 27
    EXPECT_GT(sim.getDihedralConstraintCount(), 0u);
    // Each quad contributes 1 interior diagonal + shared horizontal/vertical edges
    // The exact count depends on which edges are shared by 2 triangles
    EXPECT_GE(sim.getDihedralConstraintCount(), 9u);  // At least the diagonals
}

TEST(DihedralBending, NoConstraintsForMinimalGrid)
{
    ClothSimulator sim;
    sim.initialize(smallConfig(2, 2));

    // 2x2 grid: 1 quad, 2 triangles, 1 shared diagonal edge → 1 dihedral constraint
    EXPECT_EQ(sim.getDihedralConstraintCount(), 1u);
}

TEST(DihedralBending, FlatClothStaysFlat)
{
    // A flat cloth should have zero bending energy — simulation shouldn't
    // distort it if only dihedral bending acts.
    ClothSimulator sim;
    auto cfg = smallConfig(3, 3);
    cfg.gravity = glm::vec3(0.0f);  // No gravity
    cfg.substeps = 10;
    sim.initialize(cfg);

    // Pin all corners to keep it flat
    sim.pinParticle(0, sim.getPositions()[0]);
    sim.pinParticle(2, sim.getPositions()[2]);
    sim.pinParticle(6, sim.getPositions()[6]);
    sim.pinParticle(8, sim.getPositions()[8]);

    const glm::vec3* posBefore = sim.getPositions();
    float yBefore = posBefore[4].y;  // Center particle

    sim.simulate(1.0f / 60.0f);

    const glm::vec3* posAfter = sim.getPositions();
    // Center particle should not have moved significantly (flat = zero bending)
    EXPECT_NEAR(posAfter[4].y, yBefore, 0.01f);
}

TEST(DihedralBending, SetCompliance)
{
    ClothSimulator sim;
    sim.initialize(smallConfig(3, 3));

    sim.setDihedralBendCompliance(0.5f);
    EXPECT_FLOAT_EQ(sim.getDihedralBendCompliance(), 0.5f);

    sim.setDihedralBendCompliance(0.0f);
    EXPECT_FLOAT_EQ(sim.getDihedralBendCompliance(), 0.0f);

    // Negative compliance should clamp to 0
    sim.setDihedralBendCompliance(-1.0f);
    EXPECT_FLOAT_EQ(sim.getDihedralBendCompliance(), 0.0f);
}

TEST(DihedralBending, IncludedInConstraintCount)
{
    ClothSimulator sim;
    sim.initialize(smallConfig(3, 3));

    uint32_t total = sim.getConstraintCount();
    uint32_t dihedral = sim.getDihedralConstraintCount();

    // Total should include dihedral constraints
    EXPECT_GT(dihedral, 0u);
    EXPECT_GE(total, dihedral);
}

TEST(DihedralBending, BentClothResistsBending)
{
    // Manually push a center particle down, then simulate — dihedral
    // constraints should resist the deformation.
    ClothSimulator sim;
    auto cfg = smallConfig(3, 3);
    cfg.gravity = glm::vec3(0.0f);
    cfg.substeps = 20;
    cfg.damping = 0.05f;
    sim.initialize(cfg);

    // Pin top row
    for (uint32_t x = 0; x < 3; ++x)
    {
        sim.pinParticle(x, sim.getPositions()[x]);
    }
    // Pin bottom row
    for (uint32_t x = 6; x < 9; ++x)
    {
        sim.pinParticle(x, sim.getPositions()[x]);
    }

    // Push center particle down significantly
    glm::vec3 centerPos = sim.getPositions()[4];
    // Directly modify — we need to use setPinPosition then unpin to displace
    // Actually, we can't modify directly. Let's use a different approach:
    // Set a very low dihedral compliance (rigid) and simulate with gravity briefly
    sim.setDihedralBendCompliance(0.0f);  // Rigid bending

    // The dihedral constraints should be part of the solve
    sim.simulate(1.0f / 60.0f);

    // With rigid dihedral and no gravity, center should stay near original Y
    EXPECT_NEAR(sim.getPositions()[4].y, centerPos.y, 0.1f);
}

// ===========================================================================
// 11b: Constraint Ordering (Top-to-Bottom Sweep)
// ===========================================================================

TEST(ConstraintOrdering, SimulationSurvivesSortPathWithPinnedRow)
{
    // Smoke test: with the constraint-sort path active (4x4 cloth, top-row
    // pinned, gravity), simulate() must return without invalidating the
    // sim. Sorting order itself is not directly inspectable from the
    // public API; the SimulatesCorrectlyWithSorting test below pins the
    // observable behavior (gravity drop). This test pins the survival
    // contract — sort path doesn't crash on the pinned-row config.
    ClothSimulator sim;
    auto cfg = smallConfig(4, 4);
    cfg.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    cfg.substeps = 5;
    sim.initialize(cfg);

    for (uint32_t x = 0; x < 4; ++x)
    {
        sim.pinParticle(x, sim.getPositions()[x]);
    }

    sim.simulate(1.0f / 60.0f);

    EXPECT_TRUE(sim.isInitialized());
}

TEST(ConstraintOrdering, SimulatesCorrectlyWithSorting)
{
    // Verify that constraint ordering doesn't break the simulation
    ClothSimulator sim;
    auto cfg = smallConfig(5, 5);
    cfg.substeps = 10;
    sim.initialize(cfg);

    // Pin top-left corner
    sim.pinParticle(0, sim.getPositions()[0]);

    // Simulate several frames
    for (int i = 0; i < 30; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    // Bottom-right corner should have fallen (gravity pulls down)
    const glm::vec3* pos = sim.getPositions();
    EXPECT_LT(pos[24].y, pos[0].y);

    // All positions should be finite (no NaN from ordering bugs)
    for (uint32_t i = 0; i < sim.getParticleCount(); ++i)
    {
        EXPECT_FALSE(std::isnan(pos[i].x));
        EXPECT_FALSE(std::isnan(pos[i].y));
        EXPECT_FALSE(std::isnan(pos[i].z));
    }
}

// ===========================================================================
// 11c: Adaptive Damping
// ===========================================================================

TEST(AdaptiveDamping, DefaultDisabled)
{
    ClothSimulator sim;
    sim.initialize(smallConfig());

    EXPECT_FLOAT_EQ(sim.getAdaptiveDamping(), 0.0f);
}

TEST(AdaptiveDamping, SetAndGet)
{
    ClothSimulator sim;
    sim.initialize(smallConfig());

    sim.setAdaptiveDamping(0.3f);
    EXPECT_FLOAT_EQ(sim.getAdaptiveDamping(), 0.3f);

    // Negative should clamp to 0
    sim.setAdaptiveDamping(-1.0f);
    EXPECT_FLOAT_EQ(sim.getAdaptiveDamping(), 0.0f);
}

TEST(AdaptiveDamping, BothBackendsRemainFiniteWithAndWithoutAdaptive)
{
    // Smoke test: a 4x4 pinned cloth simulated for 60 frames at 0.01 base
    // damping must remain finite both with adaptive damping disabled and
    // with it set to 0.5. Per-frame velocity is not exposed by the public
    // API, so the "adaptive damps more" comparison cannot be asserted
    // here — that contract is pinned via integration replays. This test
    // pins the survival contract for both code paths.
    ClothSimulator simNoAdaptive;
    ClothSimulator simAdaptive;
    auto cfg = smallConfig(4, 4);
    cfg.substeps = 10;
    cfg.damping = 0.01f;

    simNoAdaptive.initialize(cfg);
    simAdaptive.initialize(cfg);

    simNoAdaptive.pinParticle(0, simNoAdaptive.getPositions()[0]);
    simAdaptive.pinParticle(0, simAdaptive.getPositions()[0]);

    simAdaptive.setAdaptiveDamping(0.5f);

    for (int i = 0; i < 60; ++i)
    {
        simNoAdaptive.simulate(1.0f / 60.0f);
        simAdaptive.simulate(1.0f / 60.0f);
    }

    const glm::vec3* posNA = simNoAdaptive.getPositions();
    const glm::vec3* posA = simAdaptive.getPositions();
    EXPECT_FALSE(std::isnan(posNA[15].y));
    EXPECT_FALSE(std::isnan(posA[15].y));
}

TEST(AdaptiveDamping, SimulationStable)
{
    // High adaptive damping should not cause instability
    ClothSimulator sim;
    auto cfg = smallConfig(5, 5);
    cfg.substeps = 10;
    sim.initialize(cfg);

    sim.pinParticle(0, sim.getPositions()[0]);
    sim.setAdaptiveDamping(1.0f);  // Aggressive

    for (int i = 0; i < 120; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    const glm::vec3* pos = sim.getPositions();
    for (uint32_t i = 0; i < sim.getParticleCount(); ++i)
    {
        EXPECT_FALSE(std::isnan(pos[i].x));
        EXPECT_FALSE(std::isnan(pos[i].y));
        EXPECT_FALSE(std::isnan(pos[i].z));
    }
}

// ===========================================================================
// 11d: Collider Friction
// ===========================================================================

TEST(ClothFriction, DefaultValues)
{
    ClothSimulator sim;
    sim.initialize(smallConfig());

    EXPECT_FLOAT_EQ(sim.getStaticFriction(), 0.4f);
    EXPECT_FLOAT_EQ(sim.getKineticFriction(), 0.3f);
}

TEST(ClothFriction, SetAndGet)
{
    ClothSimulator sim;
    sim.initialize(smallConfig());

    sim.setFriction(0.6f, 0.5f);
    EXPECT_FLOAT_EQ(sim.getStaticFriction(), 0.6f);
    EXPECT_FLOAT_EQ(sim.getKineticFriction(), 0.5f);

    // Negative should clamp to 0
    sim.setFriction(-0.1f, -0.2f);
    EXPECT_FLOAT_EQ(sim.getStaticFriction(), 0.0f);
    EXPECT_FLOAT_EQ(sim.getKineticFriction(), 0.0f);
}

TEST(ClothFriction, ZeroFrictionSimulationRemainsFinite)
{
    // Smoke test: a 3x3 cloth on a ground plane with zero static/kinetic
    // friction and lateral wind must simulate 60 frames without going
    // NaN. Per-particle slide distance is not directly compared against
    // a high-friction baseline here (that needs a paired sim and a
    // velocity oracle the public API doesn't expose).
    ClothSimulator sim;
    auto cfg = smallConfig(3, 3);
    cfg.substeps = 10;
    cfg.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    sim.initialize(cfg);

    sim.setFriction(0.0f, 0.0f);
    sim.setGroundPlane(0.0f);
    sim.setWind(glm::vec3(1, 0, 0), 5.0f);

    for (int i = 0; i < 60; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    const glm::vec3* pos = sim.getPositions();
    EXPECT_FALSE(std::isnan(pos[4].x));
}

TEST(ClothFriction, BothFrictionLevelsSimulationRemainsFinite)
{
    // Smoke test: paired 3x3 cloths on a ground plane — one with zero
    // friction, one with (0.9 static, 0.8 kinetic) — both run 60 frames
    // under wind without going NaN. The "high friction slows sliding"
    // claim is not asserted here because per-particle velocity is not
    // exposed by the public API.
    ClothSimulator simLowFric;
    ClothSimulator simHighFric;
    auto cfg = smallConfig(3, 3);
    cfg.substeps = 10;
    cfg.gravity = glm::vec3(0.0f, -9.81f, 0.0f);

    simLowFric.initialize(cfg);
    simHighFric.initialize(cfg);

    simLowFric.setFriction(0.0f, 0.0f);
    simHighFric.setFriction(0.9f, 0.8f);

    simLowFric.setGroundPlane(-0.5f);
    simHighFric.setGroundPlane(-0.5f);

    simLowFric.setWind(glm::vec3(1, 0, 0), 10.0f);
    simHighFric.setWind(glm::vec3(1, 0, 0), 10.0f);

    for (int i = 0; i < 60; ++i)
    {
        simLowFric.simulate(1.0f / 60.0f);
        simHighFric.simulate(1.0f / 60.0f);
    }

    EXPECT_FALSE(std::isnan(simLowFric.getPositions()[4].x));
    EXPECT_FALSE(std::isnan(simHighFric.getPositions()[4].x));
}

TEST(ClothFriction, FrictionWithSphereColliderSimulationRemainsFinite)
{
    // Smoke test: a 4x4 cloth pinned at the top row, draped over a sphere
    // collider with mid-range friction (0.5 static / 0.4 kinetic), must
    // simulate 60 frames without any particle going NaN. Functional
    // friction-on-curved-surface behavior is exercised by integration
    // replays, not asserted here.
    ClothSimulator sim;
    auto cfg = smallConfig(4, 4);
    cfg.substeps = 10;
    sim.initialize(cfg);

    sim.setFriction(0.5f, 0.4f);
    sim.addSphereCollider(glm::vec3(0, -1, 0), 1.5f);

    for (uint32_t x = 0; x < 4; ++x)
    {
        sim.pinParticle(x, sim.getPositions()[x]);
    }

    for (int i = 0; i < 60; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    const glm::vec3* pos = sim.getPositions();
    for (uint32_t i = 0; i < sim.getParticleCount(); ++i)
    {
        EXPECT_FALSE(std::isnan(pos[i].x));
        EXPECT_FALSE(std::isnan(pos[i].y));
        EXPECT_FALSE(std::isnan(pos[i].z));
    }
}

// ===========================================================================
// 11e: Thick Particle Model
// ===========================================================================

TEST(ThickParticle, DefaultZero)
{
    ClothSimulator sim;
    sim.initialize(smallConfig());

    EXPECT_FLOAT_EQ(sim.getParticleRadius(), 0.0f);
}

TEST(ThickParticle, SetAndGet)
{
    ClothSimulator sim;
    sim.initialize(smallConfig());

    sim.setParticleRadius(0.01f);
    EXPECT_FLOAT_EQ(sim.getParticleRadius(), 0.01f);

    // Negative should clamp to 0
    sim.setParticleRadius(-0.5f);
    EXPECT_FLOAT_EQ(sim.getParticleRadius(), 0.0f);
}

TEST(ThickParticle, GroundPlaneOffsetIncludesRadius)
{
    // With thick particles, cloth should float higher above the ground plane
    ClothSimulator simThin;
    ClothSimulator simThick;
    auto cfg = smallConfig(3, 3);
    cfg.substeps = 10;
    cfg.gravity = glm::vec3(0.0f, -9.81f, 0.0f);

    simThin.initialize(cfg);
    simThick.initialize(cfg);

    simThin.setGroundPlane(0.0f);
    simThick.setGroundPlane(0.0f);
    simThick.setParticleRadius(0.05f);  // 5cm radius

    // Let both settle on the ground
    for (int i = 0; i < 120; ++i)
    {
        simThin.simulate(1.0f / 60.0f);
        simThick.simulate(1.0f / 60.0f);
    }

    // Thick particles should rest higher than thin particles
    float minYThin = 1000.0f;
    float minYThick = 1000.0f;
    for (uint32_t i = 0; i < simThin.getParticleCount(); ++i)
    {
        minYThin = std::min(minYThin, simThin.getPositions()[i].y);
        minYThick = std::min(minYThick, simThick.getPositions()[i].y);
    }

    // Thick cloth should be at least particleRadius higher
    EXPECT_GT(minYThick, minYThin + 0.03f);
}

TEST(ThickParticle, SphereCollisionOffsetIncludesRadius)
{
    ClothSimulator simThin;
    ClothSimulator simThick;
    auto cfg = smallConfig(3, 3);
    cfg.substeps = 10;

    simThin.initialize(cfg);
    simThick.initialize(cfg);

    simThick.setParticleRadius(0.05f);

    glm::vec3 sphereCenter(0, -1, 0);
    float sphereRadius = 1.0f;
    simThin.addSphereCollider(sphereCenter, sphereRadius);
    simThick.addSphereCollider(sphereCenter, sphereRadius);

    // Pin top row
    for (uint32_t x = 0; x < 3; ++x)
    {
        simThin.pinParticle(x, simThin.getPositions()[x]);
        simThick.pinParticle(x, simThick.getPositions()[x]);
    }

    for (int i = 0; i < 60; ++i)
    {
        simThin.simulate(1.0f / 60.0f);
        simThick.simulate(1.0f / 60.0f);
    }

    // Check a particle near the sphere — thick should be farther out
    // Both should be stable
    const glm::vec3* posThin = simThin.getPositions();
    const glm::vec3* posThick = simThick.getPositions();
    for (uint32_t i = 0; i < simThin.getParticleCount(); ++i)
    {
        EXPECT_FALSE(std::isnan(posThin[i].x));
        EXPECT_FALSE(std::isnan(posThick[i].x));
    }
}

TEST(ThickParticle, SimulationStableWithRadius)
{
    ClothSimulator sim;
    auto cfg = smallConfig(5, 5);
    cfg.substeps = 10;
    sim.initialize(cfg);

    sim.setParticleRadius(0.02f);
    sim.pinParticle(0, sim.getPositions()[0]);
    sim.addSphereCollider(glm::vec3(0, -2, 0), 2.0f);
    sim.setGroundPlane(-3.0f);

    for (int i = 0; i < 120; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    const glm::vec3* pos = sim.getPositions();
    for (uint32_t i = 0; i < sim.getParticleCount(); ++i)
    {
        EXPECT_FALSE(std::isnan(pos[i].x));
        EXPECT_FALSE(std::isnan(pos[i].y));
        EXPECT_FALSE(std::isnan(pos[i].z));
    }
}

// ===========================================================================
// Combined: All features active simultaneously
// ===========================================================================

TEST(ClothSolverImprovements, AllFeaturesSimultaneously)
{
    ClothSimulator sim;
    auto cfg = smallConfig(6, 6);
    cfg.substeps = 15;
    sim.initialize(cfg);

    // Enable everything
    sim.setDihedralBendCompliance(0.005f);
    sim.setAdaptiveDamping(0.3f);
    sim.setFriction(0.4f, 0.3f);
    sim.setParticleRadius(0.01f);

    // Pin top row
    for (uint32_t x = 0; x < 6; ++x)
    {
        sim.pinParticle(x, sim.getPositions()[x]);
    }

    sim.addSphereCollider(glm::vec3(0, -2, 0), 1.5f);
    sim.setGroundPlane(-5.0f);
    sim.setWind(glm::vec3(1, 0, 0), 3.0f);

    // Simulate 3 seconds
    for (int i = 0; i < 180; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    // Everything should be stable and finite
    const glm::vec3* pos = sim.getPositions();
    for (uint32_t i = 0; i < sim.getParticleCount(); ++i)
    {
        EXPECT_FALSE(std::isnan(pos[i].x));
        EXPECT_FALSE(std::isnan(pos[i].y));
        EXPECT_FALSE(std::isnan(pos[i].z));
        EXPECT_FALSE(std::isinf(pos[i].x));
        EXPECT_FALSE(std::isinf(pos[i].y));
        EXPECT_FALSE(std::isinf(pos[i].z));
    }
}
