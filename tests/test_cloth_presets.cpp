// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_presets.cpp
/// @brief Unit tests for cloth presets and ClothSimulator live parameter updates.
#include <gtest/gtest.h>

#include "physics/cloth_presets.h"
#include "physics/cloth_simulator.h"

#include <cstring>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Preset validation
// ---------------------------------------------------------------------------

TEST(ClothPresets, AllPresetsHavePositiveMass)
{
    auto types = {ClothPresetType::LINEN_CURTAIN, ClothPresetType::TENT_FABRIC,
                  ClothPresetType::BANNER, ClothPresetType::HEAVY_DRAPE,
                  ClothPresetType::STIFF_FENCE};

    for (auto type : types)
    {
        auto preset = ClothPresets::getPresetConfig(type);
        EXPECT_GT(preset.solver.particleMass, 0.0f)
            << "Preset " << ClothPresets::getPresetName(type) << " has non-positive mass";
    }
}

TEST(ClothPresets, AllPresetsHavePositiveSubsteps)
{
    auto types = {ClothPresetType::LINEN_CURTAIN, ClothPresetType::TENT_FABRIC,
                  ClothPresetType::BANNER, ClothPresetType::HEAVY_DRAPE,
                  ClothPresetType::STIFF_FENCE};

    for (auto type : types)
    {
        auto preset = ClothPresets::getPresetConfig(type);
        EXPECT_GE(preset.solver.substeps, 1)
            << "Preset " << ClothPresets::getPresetName(type) << " has invalid substeps";
    }
}

TEST(ClothPresets, AllPresetsHaveNonNegativeCompliance)
{
    auto types = {ClothPresetType::LINEN_CURTAIN, ClothPresetType::TENT_FABRIC,
                  ClothPresetType::BANNER, ClothPresetType::HEAVY_DRAPE,
                  ClothPresetType::STIFF_FENCE};

    for (auto type : types)
    {
        auto preset = ClothPresets::getPresetConfig(type);
        EXPECT_GE(preset.solver.stretchCompliance, 0.0f);
        EXPECT_GE(preset.solver.shearCompliance, 0.0f);
        EXPECT_GE(preset.solver.bendCompliance, 0.0f);
    }
}

TEST(ClothPresets, PresetsCanInitializeSimulator)
{
    auto types = {ClothPresetType::LINEN_CURTAIN, ClothPresetType::TENT_FABRIC,
                  ClothPresetType::BANNER, ClothPresetType::HEAVY_DRAPE,
                  ClothPresetType::STIFF_FENCE};

    for (auto type : types)
    {
        auto preset = ClothPresets::getPresetConfig(type);
        // Set a small grid (don't use preset's default 20x20 — just verify it can init)
        preset.solver.width = 5;
        preset.solver.height = 5;

        ClothSimulator sim;
        sim.initialize(preset.solver);
        EXPECT_TRUE(sim.isInitialized())
            << "Preset " << ClothPresets::getPresetName(type) << " failed to initialize";
        EXPECT_EQ(sim.getParticleCount(), 25u);
    }
}

TEST(ClothPresets, PresetNamesAreNonEmpty)
{
    for (int i = 0; i < static_cast<int>(ClothPresetType::COUNT); ++i)
    {
        auto type = static_cast<ClothPresetType>(i);
        const char* name = ClothPresets::getPresetName(type);
        EXPECT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0u)
            << "Preset index " << i << " has empty name";
    }
}

TEST(ClothPresets, CustomReturnsLinenCurtainDefaults)
{
    auto custom = ClothPresets::getPresetConfig(ClothPresetType::CUSTOM);
    auto linen = ClothPresets::linenCurtain();
    EXPECT_FLOAT_EQ(custom.solver.particleMass, linen.solver.particleMass);
    EXPECT_FLOAT_EQ(custom.solver.bendCompliance, linen.solver.bendCompliance);
}

TEST(ClothPresets, PresetsHaveDifferentParameters)
{
    auto linen = ClothPresets::linenCurtain();
    auto tent = ClothPresets::tentFabric();
    auto banner = ClothPresets::banner();

    // They should have meaningfully different parameters
    EXPECT_NE(linen.solver.particleMass, tent.solver.particleMass);
    EXPECT_NE(linen.solver.bendCompliance, tent.solver.bendCompliance);
    EXPECT_NE(banner.windStrength, tent.windStrength);
}

// ---------------------------------------------------------------------------
// Live parameter updates
// ---------------------------------------------------------------------------

TEST(ClothSimulator, ResetRestoresPositions)
{
    ClothConfig cfg;
    cfg.width = 5;
    cfg.height = 5;
    cfg.spacing = 0.5f;
    cfg.particleMass = 0.1f;
    cfg.substeps = 3;

    ClothSimulator sim;
    sim.initialize(cfg);

    // Record initial positions
    uint32_t count = sim.getParticleCount();
    std::vector<glm::vec3> initialPos(sim.getPositions(),
                                       sim.getPositions() + count);

    // Simulate for several frames so particles move
    for (int i = 0; i < 10; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    // Verify particles actually moved
    bool anyMoved = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (glm::length(sim.getPositions()[i] - initialPos[i]) > 0.001f)
        {
            anyMoved = true;
            break;
        }
    }
    EXPECT_TRUE(anyMoved) << "Particles should have moved after simulation";

    // Reset
    sim.reset();

    // Verify positions are restored
    for (uint32_t i = 0; i < count; ++i)
    {
        EXPECT_NEAR(sim.getPositions()[i].x, initialPos[i].x, 1e-5f);
        EXPECT_NEAR(sim.getPositions()[i].y, initialPos[i].y, 1e-5f);
        EXPECT_NEAR(sim.getPositions()[i].z, initialPos[i].z, 1e-5f);
    }
}

TEST(ClothSimulator, ResetPreservesPinPositions)
{
    ClothConfig cfg;
    cfg.width = 5;
    cfg.height = 5;
    cfg.spacing = 0.5f;
    cfg.particleMass = 0.1f;
    cfg.substeps = 3;

    ClothSimulator sim;
    sim.initialize(cfg);

    // Pin a particle at a custom position (different from initial)
    glm::vec3 pinPos(10.0f, 20.0f, 30.0f);
    sim.pinParticle(0, pinPos);

    // Simulate
    for (int i = 0; i < 5; ++i)
    {
        sim.simulate(1.0f / 60.0f);
    }

    // Reset
    sim.reset();

    // Pinned particle should be at pin position, not initial position
    EXPECT_NEAR(sim.getPositions()[0].x, pinPos.x, 1e-5f);
    EXPECT_NEAR(sim.getPositions()[0].y, pinPos.y, 1e-5f);
    EXPECT_NEAR(sim.getPositions()[0].z, pinPos.z, 1e-5f);
}

TEST(ClothSimulator, LiveStretchComplianceUpdate)
{
    ClothConfig cfg;
    cfg.width = 5;
    cfg.height = 5;
    cfg.spacing = 0.5f;
    cfg.stretchCompliance = 0.0f;

    ClothSimulator sim;
    sim.initialize(cfg);

    // Update compliance
    sim.setStretchCompliance(0.005f);
    EXPECT_FLOAT_EQ(sim.getConfig().stretchCompliance, 0.005f);
}

TEST(ClothSimulator, LiveShearComplianceUpdate)
{
    ClothConfig cfg;
    cfg.width = 5;
    cfg.height = 5;
    cfg.spacing = 0.5f;

    ClothSimulator sim;
    sim.initialize(cfg);

    sim.setShearCompliance(0.05f);
    EXPECT_FLOAT_EQ(sim.getConfig().shearCompliance, 0.05f);
}

TEST(ClothSimulator, LiveBendComplianceUpdate)
{
    ClothConfig cfg;
    cfg.width = 5;
    cfg.height = 5;
    cfg.spacing = 0.5f;

    ClothSimulator sim;
    sim.initialize(cfg);

    sim.setBendCompliance(1.5f);
    EXPECT_FLOAT_EQ(sim.getConfig().bendCompliance, 1.5f);
}

TEST(ClothSimulator, LiveMassUpdate)
{
    ClothConfig cfg;
    cfg.width = 3;
    cfg.height = 3;
    cfg.spacing = 0.5f;
    cfg.particleMass = 0.1f;

    ClothSimulator sim;
    sim.initialize(cfg);

    sim.setParticleMass(0.5f);
    EXPECT_FLOAT_EQ(sim.getConfig().particleMass, 0.5f);
}

TEST(ClothSimulator, LiveDampingUpdate)
{
    ClothConfig cfg;
    cfg.width = 3;
    cfg.height = 3;
    cfg.spacing = 0.5f;
    cfg.damping = 0.01f;

    ClothSimulator sim;
    sim.initialize(cfg);

    sim.setDamping(0.15f);
    EXPECT_FLOAT_EQ(sim.getConfig().damping, 0.15f);
}

TEST(ClothSimulator, DampingClampedToValidRange)
{
    ClothConfig cfg;
    cfg.width = 3;
    cfg.height = 3;
    cfg.spacing = 0.5f;

    ClothSimulator sim;
    sim.initialize(cfg);

    sim.setDamping(-0.5f);
    EXPECT_FLOAT_EQ(sim.getConfig().damping, 0.0f);

    sim.setDamping(5.0f);
    EXPECT_FLOAT_EQ(sim.getConfig().damping, 1.0f);
}

TEST(ClothSimulator, NegativeMassIgnored)
{
    ClothConfig cfg;
    cfg.width = 3;
    cfg.height = 3;
    cfg.spacing = 0.5f;
    cfg.particleMass = 0.1f;

    ClothSimulator sim;
    sim.initialize(cfg);

    sim.setParticleMass(-0.5f);
    // Should remain unchanged
    EXPECT_FLOAT_EQ(sim.getConfig().particleMass, 0.1f);
}

TEST(ClothSimulator, GetPinnedCount)
{
    ClothConfig cfg;
    cfg.width = 5;
    cfg.height = 5;
    cfg.spacing = 0.5f;

    ClothSimulator sim;
    sim.initialize(cfg);

    EXPECT_EQ(sim.getPinnedCount(), 0u);

    sim.pinParticle(0, glm::vec3(0.0f));
    sim.pinParticle(1, glm::vec3(1.0f));
    EXPECT_EQ(sim.getPinnedCount(), 2u);

    sim.unpinParticle(0);
    EXPECT_EQ(sim.getPinnedCount(), 1u);
}

TEST(ClothSimulator, GetConstraintCount)
{
    ClothConfig cfg;
    cfg.width = 3;
    cfg.height = 3;
    cfg.spacing = 0.5f;

    ClothSimulator sim;
    sim.initialize(cfg);

    // 3x3 grid: stretch=12, shear=8, bend=6 = 26 total
    EXPECT_GT(sim.getConstraintCount(), 0u);
}

TEST(ClothSimulator, WindDirectionAndStrengthGetters)
{
    ClothConfig cfg;
    cfg.width = 3;
    cfg.height = 3;
    cfg.spacing = 0.5f;

    ClothSimulator sim;
    sim.initialize(cfg);

    sim.setWind(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f);
    EXPECT_NEAR(sim.getWindDirection().x, 1.0f, 1e-5f);
    EXPECT_NEAR(sim.getWindStrength(), 5.0f, 1e-5f);
    EXPECT_NEAR(sim.getDragCoefficient(), 1.0f, 1e-5f);  // default

    sim.setDragCoefficient(3.0f);
    EXPECT_NEAR(sim.getDragCoefficient(), 3.0f, 1e-5f);
}

TEST(ClothSimulator, MassUpdatePreservesPinnedParticles)
{
    ClothConfig cfg;
    cfg.width = 3;
    cfg.height = 3;
    cfg.spacing = 0.5f;
    cfg.particleMass = 0.1f;

    ClothSimulator sim;
    sim.initialize(cfg);

    // Pin particle 0
    sim.pinParticle(0, glm::vec3(0.0f));
    EXPECT_TRUE(sim.isParticlePinned(0));

    // Update mass — pinned particle should remain pinned
    sim.setParticleMass(0.5f);
    EXPECT_TRUE(sim.isParticlePinned(0));
}
