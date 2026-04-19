// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_solver_backend.cpp
/// @brief Tests that ClothSimulator satisfies the IClothSolverBackend contract.
///
/// Phase 9B Step 1 introduces `IClothSolverBackend` as a thin abstraction that
/// the upcoming `GpuClothSimulator` will also implement. These tests pin the
/// contract: any backend must be constructible behind the interface pointer
/// and round-trip lifecycle calls without UB.

#include <gtest/gtest.h>

#include "physics/cloth_simulator.h"
#include "physics/cloth_solver_backend.h"

#include <memory>

using namespace Vestige;

TEST(ClothSolverBackend, ClothSimulatorIsAnIClothSolverBackend)
{
    // Compile-time check via base-pointer construction.
    std::unique_ptr<IClothSolverBackend> backend = std::make_unique<ClothSimulator>();
    EXPECT_NE(backend.get(), nullptr);
    EXPECT_FALSE(backend->isInitialized());
}

TEST(ClothSolverBackend, InitializeThroughInterfaceWorks)
{
    std::unique_ptr<IClothSolverBackend> backend = std::make_unique<ClothSimulator>();

    ClothConfig cfg;
    cfg.width = 4;
    cfg.height = 4;
    cfg.spacing = 0.1f;

    backend->initialize(cfg, /*seed=*/1);

    EXPECT_TRUE(backend->isInitialized());
    EXPECT_EQ(backend->getParticleCount(), 16u);
    EXPECT_EQ(backend->getGridWidth(), 4u);
    EXPECT_EQ(backend->getGridHeight(), 4u);
    EXPECT_NE(backend->getPositions(), nullptr);
    EXPECT_NE(backend->getNormals(), nullptr);

    // Two triangles per quad, 9 quads in a 4x4 grid → 18 triangles → 54 indices.
    EXPECT_EQ(backend->getIndices().size(), 54u);
    EXPECT_EQ(backend->getTexCoords().size(), 16u);
}

TEST(ClothSolverBackend, SimulateAndResetThroughInterface)
{
    std::unique_ptr<IClothSolverBackend> backend = std::make_unique<ClothSimulator>();

    ClothConfig cfg;
    cfg.width = 4;
    cfg.height = 4;
    cfg.spacing = 0.1f;
    backend->initialize(cfg);

    // Snapshot the unsimulated bottom-row position.
    const glm::vec3 initialBottom = backend->getPositions()[0];

    // Simulate a few steps under default gravity. Particles should move.
    for (int i = 0; i < 30; ++i) backend->simulate(1.0f / 60.0f);

    const glm::vec3 movedBottom = backend->getPositions()[0];
    EXPECT_LT(movedBottom.y, initialBottom.y)
        << "particle should have fallen under gravity";

    // Reset returns to the initial pose.
    backend->reset();
    const glm::vec3 resetBottom = backend->getPositions()[0];
    EXPECT_FLOAT_EQ(resetBottom.x, initialBottom.x);
    EXPECT_FLOAT_EQ(resetBottom.y, initialBottom.y);
    EXPECT_FLOAT_EQ(resetBottom.z, initialBottom.z);
}

TEST(ClothSolverBackend, DestructionThroughInterfaceIsSafe)
{
    // Virtual destructor sanity — destroying via base pointer must run
    // ClothSimulator's destructor (releasing internal vectors).
    auto backend = std::unique_ptr<IClothSolverBackend>(new ClothSimulator());
    ClothConfig cfg;
    cfg.width = 8;
    cfg.height = 8;
    backend->initialize(cfg);
    EXPECT_TRUE(backend->isInitialized());
    // No assertion — relies on Address/UB sanitizers in CI to catch
    // double-free or non-virtual-dtor leaks.
}
