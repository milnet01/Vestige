// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_cloth_simulator.cpp
/// @brief CPU-side tests for the Phase 9B GpuClothSimulator skeleton.
///
/// These tests exercise the no-GL-context path: default state, polymorphic
/// construction, isSupported() probe behaviour. The GL-resident behaviour
/// (SSBO allocation, GL error checks, no-op simulate) is verified by
/// running the engine itself — no test in this suite establishes a GL
/// context (`grep glfwInit tests/`), and adding one purely for cloth tests
/// would duplicate the manual-launch verification path.

#include <gtest/gtest.h>

#include "physics/cloth_solver_backend.h"
#include "physics/gpu_cloth_simulator.h"

#include <memory>

using namespace Vestige;

TEST(GpuClothSimulator, DefaultStateMatchesUninitialised)
{
    GpuClothSimulator sim;
    EXPECT_FALSE(sim.isInitialized());
    EXPECT_EQ(sim.getParticleCount(), 0u);
    EXPECT_EQ(sim.getGridWidth(),  0u);
    EXPECT_EQ(sim.getGridHeight(), 0u);
    EXPECT_EQ(sim.getPositions(),  nullptr);
    EXPECT_EQ(sim.getNormals(),    nullptr);
    EXPECT_TRUE(sim.getIndices().empty());
    EXPECT_TRUE(sim.getTexCoords().empty());
}

TEST(GpuClothSimulator, IsSupportedReturnsFalseWithoutGlContext)
{
    // The unit-test binary doesn't establish a GL context. The probe must
    // gracefully report "not supported" rather than crash on glGetIntegerv.
    EXPECT_FALSE(GpuClothSimulator::isSupported());
}

TEST(GpuClothSimulator, IsAnIClothSolverBackend)
{
    // Compile-time check: GpuClothSimulator slots into the same interface
    // as ClothSimulator so ClothComponent can swap backends in a later step.
    std::unique_ptr<IClothSolverBackend> backend = std::make_unique<GpuClothSimulator>();
    EXPECT_NE(backend.get(), nullptr);
    EXPECT_FALSE(backend->isInitialized());
    EXPECT_EQ(backend->getParticleCount(), 0u);
}

TEST(GpuClothSimulator, BufferBindingsMatchDesignDoc)
{
    // The SSBO binding indices form part of the cross-component GLSL
    // contract (the future cloth_*.comp.glsl shaders bind to these
    // explicit numbers). Pin them so a reordering doesn't silently
    // shift the binding layout out from under the shaders.
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_POSITIONS),      0u);
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_PREV_POSITIONS), 1u);
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_VELOCITIES),     2u);
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_NORMALS),        6u);
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_INDICES),        7u);
}

TEST(GpuClothSimulator, SsboHandlesAreZeroBeforeInit)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getPositionsSSBO(),     0u);
    EXPECT_EQ(sim.getPrevPositionsSSBO(), 0u);
    EXPECT_EQ(sim.getVelocitiesSSBO(),    0u);
    EXPECT_EQ(sim.getNormalsSSBO(),       0u);
    EXPECT_EQ(sim.getIndicesSSBO(),       0u);
}

// -- Step 3 surface --

TEST(GpuClothSimulator, HasShadersDefaultsFalse)
{
    GpuClothSimulator sim;
    EXPECT_FALSE(sim.hasShaders());
}

TEST(GpuClothSimulator, ParameterSettersCompileAndAccept)
{
    // Smoke-check the per-frame parameter setters added in Step 3 — the
    // shaders consume these uniforms but the C++ accessors are pure
    // member writes so they're testable without a GL context.
    GpuClothSimulator sim;
    sim.setShaderPath("/dev/null/shaders");   // No load attempted yet
    sim.setDragCoefficient(0.5f);
    sim.setWindVelocity(glm::vec3(1.0f, 0.0f, 0.0f));
    sim.setDamping(0.02f);
    SUCCEED();
}

// -- Step 4 surface --

TEST(GpuClothSimulator, ConstraintCountIsZeroBeforeInit)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getConstraintCount(), 0u);
    EXPECT_EQ(sim.getColourCount(),     0u);
    EXPECT_EQ(sim.getConstraintsSSBO(), 0u);
}

TEST(GpuClothSimulator, SetSubstepsClampsToOne)
{
    GpuClothSimulator sim;
    sim.setSubsteps(0);
    EXPECT_EQ(sim.getSubsteps(), 1) << "substep count must be clamped to >= 1";
    sim.setSubsteps(-3);
    EXPECT_EQ(sim.getSubsteps(), 1);
    sim.setSubsteps(20);
    EXPECT_EQ(sim.getSubsteps(), 20);
}

TEST(GpuClothSimulator, BindConstraintsEnumPinned)
{
    // Constraints SSBO binding 4 is the contract with the cloth_constraints
    // compute shader. Pin it so a refactor can't silently shift it.
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_CONSTRAINTS), 4u);
}

// -- Step 6 surface --

TEST(GpuClothSimulator, BindDihedralsEnumPinned)
{
    // Dihedral SSBO binding 5 is the contract with cloth_dihedral.comp.glsl.
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_DIHEDRALS), 5u);
}

TEST(GpuClothSimulator, DihedralCountAndColoursZeroBeforeInit)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getDihedralCount(),       0u);
    EXPECT_EQ(sim.getDihedralColourCount(), 0u);
    EXPECT_EQ(sim.getDihedralsSSBO(),       0u);
}

// -- Step 7 surface --

TEST(GpuClothSimulator, ColliderDefaults)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getSphereColliderCount(), 0u);
    EXPECT_EQ(sim.getPlaneColliderCount(),  0u);
    EXPECT_FLOAT_EQ(sim.getCollisionMargin(), 0.015f);
    EXPECT_LT(sim.getGroundPlane(), -100.0f) << "ground default should be far below scene";
}

TEST(GpuClothSimulator, AddSphereColliderIncrementsCount)
{
    GpuClothSimulator sim;
    sim.addSphereCollider(glm::vec3(0.0f, 1.0f, 0.0f), 0.5f);
    EXPECT_EQ(sim.getSphereColliderCount(), 1u);
    sim.addSphereCollider(glm::vec3(2.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_EQ(sim.getSphereColliderCount(), 2u);
}

TEST(GpuClothSimulator, AddSphereColliderRejectsZeroRadius)
{
    GpuClothSimulator sim;
    sim.addSphereCollider(glm::vec3(0.0f), 0.0f);
    sim.addSphereCollider(glm::vec3(0.0f), -1.0f);
    EXPECT_EQ(sim.getSphereColliderCount(), 0u);
}

TEST(GpuClothSimulator, ClearSphereCollidersResetsCount)
{
    GpuClothSimulator sim;
    sim.addSphereCollider(glm::vec3(0.0f), 0.5f);
    sim.addSphereCollider(glm::vec3(1.0f), 0.5f);
    sim.clearSphereColliders();
    EXPECT_EQ(sim.getSphereColliderCount(), 0u);
}

TEST(GpuClothSimulator, AddPlaneColliderRejectsZeroNormal)
{
    GpuClothSimulator sim;
    EXPECT_FALSE(sim.addPlaneCollider(glm::vec3(0.0f), 1.0f));
    EXPECT_EQ(sim.getPlaneColliderCount(), 0u);
    EXPECT_TRUE(sim.addPlaneCollider(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f));
    EXPECT_EQ(sim.getPlaneColliderCount(), 1u);
}

TEST(GpuClothSimulator, BindCollidersUboEnumPinned)
{
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_COLLIDERS_UBO), 3u);
}

// -- Step 9 surface (pins + LRA, CPU-side state only) --

TEST(GpuClothSimulator, PinDefaultsAreZero)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getPinnedCount(), 0u);
    EXPECT_EQ(sim.getLraCount(),    0u);
    EXPECT_FALSE(sim.isParticlePinned(0));
}

TEST(GpuClothSimulator, BindLrasEnumPinned)
{
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_LRAS), 8u);
}

TEST(ClothConstraintGraph, GenerateLraEmptyForNoPins)
{
    std::vector<glm::vec3> positions(16, glm::vec3(0.0f));
    std::vector<uint32_t>  pins;
    std::vector<GpuLraConstraint> lras;
    generateLraConstraints(positions, pins, lras);
    EXPECT_TRUE(lras.empty());
}

TEST(ClothConstraintGraph, GenerateLraTethersEveryFreeParticle)
{
    // 4 particles in a line; pin index 0; expect 3 LRA tethers all referencing pin 0.
    std::vector<glm::vec3> positions = {
        {0,0,0}, {1,0,0}, {2,0,0}, {3,0,0}
    };
    std::vector<uint32_t> pins = {0};
    std::vector<GpuLraConstraint> lras;
    generateLraConstraints(positions, pins, lras);

    ASSERT_EQ(lras.size(), 3u);
    for (const auto& l : lras)
    {
        EXPECT_EQ(l.pinIndex, 0u);
        EXPECT_NE(l.particleIndex, 0u);
    }
    EXPECT_FLOAT_EQ(lras[0].maxDistance, 1.0f);
    EXPECT_FLOAT_EQ(lras[1].maxDistance, 2.0f);
    EXPECT_FLOAT_EQ(lras[2].maxDistance, 3.0f);
}
