// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_backend_factory.cpp
/// @brief Unit tests for the CPU↔GPU cloth-backend auto-select factory.

#include <gtest/gtest.h>

#include <limits>

#include "physics/cloth_backend_factory.h"
#include "physics/cloth_component.h"
#include "physics/gpu_cloth_simulator.h"
#include "physics/cloth_simulator.h"  // For ClothConfig

using namespace Vestige;

TEST(ClothBackendFactory, ChooseAutoBelowThresholdIsCpu)
{
    ClothConfig cfg;
    cfg.width = 16;   // 16x16 = 256 particles, below threshold
    cfg.height = 16;
    EXPECT_EQ(chooseClothBackend(cfg, ClothBackendPolicy::AUTO, /*gpuSupported=*/true),
              ClothBackendChoice::CPU);
}

TEST(ClothBackendFactory, ChooseAutoAtOrAboveThresholdIsGpu)
{
    ClothConfig cfg;
    cfg.width = 32;   // 32x32 = 1024 particles, == threshold
    cfg.height = 32;
    EXPECT_EQ(chooseClothBackend(cfg, ClothBackendPolicy::AUTO, /*gpuSupported=*/true),
              ClothBackendChoice::GPU);

    cfg.width = 64;   // 64x64 = 4096 particles, well above threshold
    cfg.height = 64;
    EXPECT_EQ(chooseClothBackend(cfg, ClothBackendPolicy::AUTO, /*gpuSupported=*/true),
              ClothBackendChoice::GPU);
}

TEST(ClothBackendFactory, ChooseAutoFallsBackToCpuWhenGpuUnsupported)
{
    ClothConfig cfg;
    cfg.width = 100;  // 10000 particles, way above threshold
    cfg.height = 100;
    EXPECT_EQ(chooseClothBackend(cfg, ClothBackendPolicy::AUTO, /*gpuSupported=*/false),
              ClothBackendChoice::CPU);
}

TEST(ClothBackendFactory, ForceCpuAlwaysReturnsCpu)
{
    ClothConfig cfg;
    cfg.width = 200;
    cfg.height = 200;
    EXPECT_EQ(chooseClothBackend(cfg, ClothBackendPolicy::FORCE_CPU, /*gpuSupported=*/true),
              ClothBackendChoice::CPU);
}

TEST(ClothBackendFactory, ForceGpuRespectsCallerEvenWhenSmall)
{
    ClothConfig cfg;
    cfg.width = 4;
    cfg.height = 4;
    EXPECT_EQ(chooseClothBackend(cfg, ClothBackendPolicy::FORCE_GPU, /*gpuSupported=*/true),
              ClothBackendChoice::GPU);
}

// lane-07 lead cloth_backend_factory.cpp:25: FORCE_GPU returned GPU even with
// no compute support, so the factory built a GpuClothSimulator that could not
// run, and its "falling back to CPU" warning was unreachable.
TEST(ClothBackendFactory, ForceGpuFallsBackToCpuWithoutCompute)
{
    ClothConfig cfg;
    cfg.width = 100;
    cfg.height = 100;
    EXPECT_EQ(chooseClothBackend(cfg, ClothBackendPolicy::FORCE_GPU, /*gpuSupported=*/false),
              ClothBackendChoice::CPU);
}

TEST(ClothBackendFactory, CreateReturnsCpuWhenNoGlContext)
{
    // No GL context in unit tests → GpuClothSimulator::isSupported() returns
    // false, so AUTO and FORCE_GPU both fall back to CPU.
    ClothConfig cfg;
    cfg.width = 100;
    cfg.height = 100;
    auto backend = createClothSolverBackend(cfg, ClothBackendPolicy::AUTO,
                                              /*shaderPath=*/"unused");
    ASSERT_NE(backend.get(), nullptr);
    // CPU backend doesn't initialize GL state at construction, so this is safe.
    EXPECT_FALSE(backend->isInitialized());
}

TEST(ClothBackendFactory, CreateForceCpuYieldsCpuBackendThatInitializes)
{
    ClothConfig cfg;
    cfg.width = 4;
    cfg.height = 4;
    cfg.spacing = 0.1f;
    auto backend = createClothSolverBackend(cfg, ClothBackendPolicy::FORCE_CPU, "");
    ASSERT_NE(backend.get(), nullptr);
    backend->initialize(cfg);
    EXPECT_TRUE(backend->isInitialized());
    EXPECT_EQ(backend->getParticleCount(), 16u);
}

TEST(ClothBackendFactory, ThresholdConstantMatchesDoc)
{
    // Sanity-pin so a refactor that bumps the threshold without updating
    // the design doc fails loudly.
    EXPECT_EQ(GPU_AUTO_SELECT_THRESHOLD, 1024u);
}

TEST(ClothBackendFactory, ChooseAutoCountsParticlesWithoutWrapping)
{
    ClothConfig cfg;
    cfg.width = 65536;   // 65536 x 65536 wraps to 0 in 32 bits
    cfg.height = 65536;
    EXPECT_EQ(chooseClothBackend(cfg, ClothBackendPolicy::AUTO, /*gpuSupported=*/true),
              ClothBackendChoice::GPU);
}

// lane-07 lead cloth_component.cpp:49: initialize() never checked whether the
// solver accepted the config, so a refused one still reported isReady().
TEST(ClothComponentInit, RefusedConfigLeavesTheComponentNotReady)
{
    ClothConfig cfg;
    cfg.width = 1;   // under the 2x2 minimum: the solver refuses it
    cfg.height = 1;
    ClothComponent component;
    component.setBackendPolicy(ClothBackendPolicy::FORCE_CPU);
    component.initialize(cfg, nullptr);
    EXPECT_FALSE(component.isReady());
}

// lane-07 lead gpu_cloth_simulator.cpp:181: the GPU backend dropped a sphere
// of radius 0 or less while the CPU backend clamped it to 1 mm and kept it.
// Both now refuse a radius that is not positive and finite.
TEST(ClothColliderParity, BothBackendsRefuseADegenerateSphere)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ClothSimulator cpu;
    GpuClothSimulator gpu;  // adding a collider needs no GL context
    for (float radius : {0.0f, -1.0f, nan})
    {
        cpu.addSphereCollider(glm::vec3(0.0f), radius);
        gpu.addSphereCollider(glm::vec3(0.0f), radius);
    }
    EXPECT_EQ(cpu.getSphereColliderCount(), 0u);
    EXPECT_EQ(gpu.getSphereColliderCount(), 0u);
    cpu.addSphereCollider(glm::vec3(0.0f), 0.5f);
    gpu.addSphereCollider(glm::vec3(0.0f), 0.5f);
    EXPECT_EQ(cpu.getSphereColliderCount(), 1u);
    EXPECT_EQ(gpu.getSphereColliderCount(), 1u);
}
