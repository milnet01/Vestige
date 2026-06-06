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

    // `seed` is the per-cloth wind-randomness seed (see IClothSolverBackend::initialize).
    // Pin it to a non-zero literal so the test is reproducible across runs.
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
    backend->initialize(cfg, /*seed=*/1);

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

// Phase 10.9 Slice 18 Ts1 cleanup: dropped the previous
// `DestructionThroughInterfaceIsSafe` test — it had no GTest
// assertions and relied on sanitiser detection without configuring a
// sanitiser. The virtual-dtor contract is enforced by the `override`
// keyword on `ClothSimulator::~ClothSimulator()` and by the
// compile-time `-Wnon-virtual-dtor` warning.

// =============================================================================
// Phase 10.9 Cl9 — convergence-accelerator API contract (clamp + precedence).
// Exercised on the CPU backend (no GL needed); the GPU backend shares the
// identical clamp/precedence logic and is additionally pinned by the
// SOR-accelerated drape test in test_cloth_cpu_gpu_parity.cpp.
// =============================================================================

TEST(ClothSolverBackendCl9, DefaultsAreOneIterationNoneMode)
{
    ClothSimulator sim;
    EXPECT_EQ(sim.getSolverIterations(), 1);
    EXPECT_EQ(sim.getConvergenceMode(), ClothConvergenceMode::None);
}

TEST(ClothSolverBackendCl9, SolverIterationsClampToRangeWhenModeNone)
{
    ClothSimulator sim;
    sim.setSolverIterations(0);
    EXPECT_EQ(sim.getSolverIterations(), 1) << "floor is 1 when mode == None";
    sim.setSolverIterations(9999);
    EXPECT_EQ(sim.getSolverIterations(), MAX_SOLVER_ITERS) << "capped at MAX_SOLVER_ITERS";
    sim.setSolverIterations(8);
    EXPECT_EQ(sim.getSolverIterations(), 8) << "in-range value kept verbatim";
}

TEST(ClothSolverBackendCl9, EnablingAModeBumpsIterationsToMinimum)
{
    ClothSimulator sim;  // default 1 iteration
    sim.setConvergenceMode(ClothConvergenceMode::SOR);
    EXPECT_EQ(sim.getSolverIterations(), CLOTH_CHEBYSHEV_DELAY + 1)
        << "enabling a mode must bump iterations above the delay S so it engages";
}

TEST(ClothSolverBackendCl9, SolverIterationsClampToSPlusOneWhileModeActive)
{
    ClothSimulator sim;
    sim.setConvergenceMode(ClothConvergenceMode::SOR);
    sim.setSolverIterations(1);
    EXPECT_EQ(sim.getSolverIterations(), CLOTH_CHEBYSHEV_DELAY + 1)
        << "a too-low value is clamped up, never silently disables the accelerator";
    sim.setSolverIterations(16);
    EXPECT_EQ(sim.getSolverIterations(), 16);
}

TEST(ClothSolverBackendCl9, ConvergenceModeRoundTrips)
{
    ClothSimulator sim;
    for (ClothConvergenceMode m : {ClothConvergenceMode::None,
                                   ClothConvergenceMode::SOR,
                                   ClothConvergenceMode::Chebyshev})
    {
        sim.setConvergenceMode(m);
        EXPECT_EQ(sim.getConvergenceMode(), m);
    }
}
