// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_domain_systems.cpp
/// @brief Tests for Phase 9B/9C domain system wrappers.
#include <gtest/gtest.h>

#include "core/i_system.h"
#include "systems/atmosphere_system.h"
#include "systems/particle_system.h"
#include "systems/water_system.h"
#include "systems/vegetation_system.h"
#include "systems/terrain_system.h"
#include "systems/cloth_system.h"
#include "systems/destruction_system.h"
#include "systems/character_system.h"
#include "systems/lighting_system.h"
#include "systems/audio_system.h"
#include "systems/ui_system.h"
#include "systems/navigation_system.h"
#include "audio/audio_source_component.h"
#include "navigation/nav_agent_component.h"
#include "navigation/nav_mesh_config.h"
#include "ui/ui_signal.h"
#include "ui/ui_element.h"
#include "ui/ui_panel.h"

using namespace Vestige;

// ==========================================================================
// Test fixture
// ==========================================================================

class DomainSystemTest : public ::testing::Test
{
protected:
    // Domain system tests only verify non-GL properties (names, types,
    // force-active, owned components). GL-dependent tests (initialize,
    // shutdown) are covered by integration tests.
};

// ==========================================================================
// AtmosphereSystem
// ==========================================================================

TEST_F(DomainSystemTest, AtmosphereSystemName)
{
    AtmosphereSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Atmosphere");
}

TEST_F(DomainSystemTest, AtmosphereSystemIsForceActive)
{
    AtmosphereSystem sys;
    EXPECT_TRUE(sys.isForceActive());
}

TEST_F(DomainSystemTest, AtmosphereSystemNoOwnedComponents)
{
    AtmosphereSystem sys;
    EXPECT_TRUE(sys.getOwnedComponentTypes().empty());
}

TEST_F(DomainSystemTest, AtmosphereSystemStartsInactive)
{
    AtmosphereSystem sys;
    EXPECT_FALSE(sys.isActive());
}

TEST_F(DomainSystemTest, AtmosphereSystemHasEnvironmentForces)
{
    AtmosphereSystem sys;
    // EnvironmentForces should exist and provide wind queries
    EXPECT_NE(&sys.getEnvironmentForces(), nullptr);
}

// ==========================================================================
// ParticleVfxSystem
// ==========================================================================

TEST_F(DomainSystemTest, ParticleSystemName)
{
    ParticleVfxSystem sys;
    EXPECT_EQ(sys.getSystemName(), "ParticleVFX");
}

TEST_F(DomainSystemTest, ParticleSystemNotForceActive)
{
    ParticleVfxSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, ParticleSystemOwnsComponents)
{
    ParticleVfxSystem sys;
    auto types = sys.getOwnedComponentTypes();
    EXPECT_EQ(types.size(), 2u);
}

TEST_F(DomainSystemTest, ParticleSystemHasRenderer)
{
    ParticleVfxSystem sys;
    EXPECT_NE(&sys.getParticleRenderer(), nullptr);
}

// ==========================================================================
// WaterSystem
// ==========================================================================

TEST_F(DomainSystemTest, WaterSystemName)
{
    WaterSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Water");
}

TEST_F(DomainSystemTest, WaterSystemNotForceActive)
{
    WaterSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, WaterSystemOwnsComponents)
{
    WaterSystem sys;
    auto types = sys.getOwnedComponentTypes();
    EXPECT_EQ(types.size(), 1u);
}

TEST_F(DomainSystemTest, WaterSystemHasRendererAndFbo)
{
    WaterSystem sys;
    EXPECT_NE(&sys.getWaterRenderer(), nullptr);
    EXPECT_NE(&sys.getWaterFbo(), nullptr);
}

// ==========================================================================
// VegetationSystem
// ==========================================================================

TEST_F(DomainSystemTest, VegetationSystemName)
{
    VegetationSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Vegetation");
}

TEST_F(DomainSystemTest, VegetationSystemNotForceActive)
{
    VegetationSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, VegetationSystemNoOwnedComponents)
{
    VegetationSystem sys;
    EXPECT_TRUE(sys.getOwnedComponentTypes().empty());
}

TEST_F(DomainSystemTest, VegetationSystemHasSubsystems)
{
    VegetationSystem sys;
    EXPECT_NE(&sys.getFoliageManager(), nullptr);
    EXPECT_NE(&sys.getFoliageRenderer(), nullptr);
    EXPECT_NE(&sys.getTreeRenderer(), nullptr);
}

// ==========================================================================
// TerrainSystem
// ==========================================================================

TEST_F(DomainSystemTest, TerrainSystemName)
{
    TerrainSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Terrain");
}

TEST_F(DomainSystemTest, TerrainSystemNotForceActive)
{
    TerrainSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, TerrainSystemNoOwnedComponents)
{
    TerrainSystem sys;
    EXPECT_TRUE(sys.getOwnedComponentTypes().empty());
}

TEST_F(DomainSystemTest, TerrainSystemHasSubsystems)
{
    TerrainSystem sys;
    EXPECT_NE(&sys.getTerrain(), nullptr);
    EXPECT_NE(&sys.getTerrainRenderer(), nullptr);
}

// ==========================================================================
// ClothSystem
// ==========================================================================

TEST_F(DomainSystemTest, ClothSystemName)
{
    ClothSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Cloth");
}

TEST_F(DomainSystemTest, ClothSystemNotForceActive)
{
    ClothSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, ClothSystemOwnsComponents)
{
    ClothSystem sys;
    auto types = sys.getOwnedComponentTypes();
    EXPECT_EQ(types.size(), 1u);
}

// ==========================================================================
// DestructionSystem
// ==========================================================================

TEST_F(DomainSystemTest, DestructionSystemName)
{
    DestructionSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Destruction");
}

TEST_F(DomainSystemTest, DestructionSystemNotForceActive)
{
    DestructionSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, DestructionSystemOwnsComponents)
{
    DestructionSystem sys;
    auto types = sys.getOwnedComponentTypes();
    EXPECT_EQ(types.size(), 2u);
}

// ==========================================================================
// CharacterSystem
// ==========================================================================

TEST_F(DomainSystemTest, CharacterSystemName)
{
    CharacterSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Character");
}

TEST_F(DomainSystemTest, CharacterSystemNotForceActive)
{
    CharacterSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, CharacterSystemNoOwnedComponents)
{
    CharacterSystem sys;
    EXPECT_TRUE(sys.getOwnedComponentTypes().empty());
}

TEST_F(DomainSystemTest, CharacterSystemHasController)
{
    CharacterSystem sys;
    EXPECT_NE(&sys.getPhysicsCharController(), nullptr);
}

// ==========================================================================
// LightingSystem
// ==========================================================================

TEST_F(DomainSystemTest, LightingSystemName)
{
    LightingSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Lighting");
}

TEST_F(DomainSystemTest, LightingSystemIsForceActive)
{
    LightingSystem sys;
    EXPECT_TRUE(sys.isForceActive());
}

TEST_F(DomainSystemTest, LightingSystemNoOwnedComponents)
{
    LightingSystem sys;
    EXPECT_TRUE(sys.getOwnedComponentTypes().empty());
}

// ==========================================================================
// AudioSystem
// ==========================================================================

TEST_F(DomainSystemTest, AudioSystemName)
{
    AudioSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Audio");
}

TEST_F(DomainSystemTest, AudioSystemNotForceActive)
{
    AudioSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, AudioSystemOwnsComponents)
{
    AudioSystem sys;
    auto types = sys.getOwnedComponentTypes();
    EXPECT_EQ(types.size(), 1u);
}

TEST_F(DomainSystemTest, AudioSystemHasAudioEngine)
{
    AudioSystem sys;
    EXPECT_NE(&sys.getAudioEngine(), nullptr);
}

TEST_F(DomainSystemTest, AudioSourceComponentDefaults)
{
    AudioSourceComponent comp;
    EXPECT_FLOAT_EQ(comp.volume, 1.0f);
    EXPECT_FLOAT_EQ(comp.pitch, 1.0f);
    EXPECT_FLOAT_EQ(comp.minDistance, 1.0f);
    EXPECT_FLOAT_EQ(comp.maxDistance, 50.0f);
    EXPECT_FALSE(comp.loop);
    EXPECT_FALSE(comp.autoPlay);
    EXPECT_TRUE(comp.spatial);
}

// ==========================================================================
// UISystem
// ==========================================================================

TEST_F(DomainSystemTest, UISystemName)
{
    UISystem sys;
    EXPECT_EQ(sys.getSystemName(), "UI");
}

TEST_F(DomainSystemTest, UISystemNotForceActive)
{
    UISystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, UISystemNoOwnedComponents)
{
    UISystem sys;
    EXPECT_TRUE(sys.getOwnedComponentTypes().empty());
}

TEST_F(DomainSystemTest, UISignalConnectAndEmit)
{
    Signal<int> signal;
    int received = 0;
    signal.connect([&received](int val) { received = val; });
    signal.emit(42);
    EXPECT_EQ(received, 42);
}

TEST_F(DomainSystemTest, UISignalMultipleSlots)
{
    Signal<> signal;
    int callCount = 0;
    signal.connect([&callCount]() { ++callCount; });
    signal.connect([&callCount]() { ++callCount; });
    signal.emit();
    EXPECT_EQ(callCount, 2);
}

TEST_F(DomainSystemTest, UIElementHitTest)
{
    UIPanel panel;
    panel.position = {100.0f, 100.0f};
    panel.size = {200.0f, 50.0f};
    panel.anchor = Anchor::TOP_LEFT;
    panel.interactive = true;

    glm::vec2 noOffset(0.0f);

    // Point inside
    EXPECT_TRUE(panel.hitTest({150.0f, 120.0f}, noOffset, 1920, 1080));
    // Point outside
    EXPECT_FALSE(panel.hitTest({50.0f, 50.0f}, noOffset, 1920, 1080));
    // Edge case: on the boundary
    EXPECT_TRUE(panel.hitTest({100.0f, 100.0f}, noOffset, 1920, 1080));
}

TEST_F(DomainSystemTest, UIElementNotInteractiveNoHit)
{
    UIPanel panel;
    panel.position = {100.0f, 100.0f};
    panel.size = {200.0f, 50.0f};
    panel.interactive = false;  // Not interactive

    glm::vec2 noOffset(0.0f);
    EXPECT_FALSE(panel.hitTest({150.0f, 120.0f}, noOffset, 1920, 1080));
}

// ==========================================================================
// NavigationSystem
// ==========================================================================

TEST_F(DomainSystemTest, NavigationSystemName)
{
    NavigationSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Navigation");
}

TEST_F(DomainSystemTest, NavigationSystemNotForceActive)
{
    NavigationSystem sys;
    EXPECT_FALSE(sys.isForceActive());
}

TEST_F(DomainSystemTest, NavigationSystemOwnsComponents)
{
    NavigationSystem sys;
    auto types = sys.getOwnedComponentTypes();
    EXPECT_EQ(types.size(), 1u);
}

TEST_F(DomainSystemTest, NavigationSystemNoMeshInitially)
{
    NavigationSystem sys;
    EXPECT_FALSE(sys.hasNavMesh());
}

TEST_F(DomainSystemTest, NavAgentComponentDefaults)
{
    NavAgentComponent comp;
    EXPECT_FLOAT_EQ(comp.radius, 0.4f);
    EXPECT_FLOAT_EQ(comp.height, 1.8f);
    EXPECT_FLOAT_EQ(comp.maxSpeed, 3.5f);
    EXPECT_TRUE(comp.hasReachedDestination());
}

TEST_F(DomainSystemTest, NavMeshConfigDefaults)
{
    NavMeshBuildConfig config;
    EXPECT_FLOAT_EQ(config.cellSize, 0.3f);
    EXPECT_FLOAT_EQ(config.agentHeight, 1.8f);
    EXPECT_FLOAT_EQ(config.agentRadius, 0.4f);
    EXPECT_FLOAT_EQ(config.agentMaxSlope, 45.0f);
    EXPECT_EQ(config.vertsPerPoly, 6);
}

// ==========================================================================
// Cross-system: all 12 systems have unique names
// ==========================================================================

TEST_F(DomainSystemTest, AllSystemsHaveUniqueNames)
{
    AtmosphereSystem atmo;
    ParticleVfxSystem particle;
    WaterSystem water;
    VegetationSystem veg;
    TerrainSystem terrain;
    ClothSystem cloth;
    DestructionSystem destruction;
    CharacterSystem character;
    LightingSystem lighting;
    AudioSystem audio;
    UISystem ui;
    NavigationSystem navigation;

    std::set<std::string> names;
    names.insert(atmo.getSystemName());
    names.insert(particle.getSystemName());
    names.insert(water.getSystemName());
    names.insert(veg.getSystemName());
    names.insert(terrain.getSystemName());
    names.insert(cloth.getSystemName());
    names.insert(destruction.getSystemName());
    names.insert(character.getSystemName());
    names.insert(lighting.getSystemName());
    names.insert(audio.getSystemName());
    names.insert(ui.getSystemName());
    names.insert(navigation.getSystemName());

    EXPECT_EQ(names.size(), 12u);
}

// ==========================================================================
// ISystem interface compliance
// ==========================================================================

TEST_F(DomainSystemTest, AllSystemsInheritFromISystem)
{
    // Verify polymorphism works — all can be stored as ISystem*
    AtmosphereSystem atmo;
    ParticleVfxSystem particle;
    WaterSystem water;
    VegetationSystem veg;
    TerrainSystem terrain;
    ClothSystem cloth;
    DestructionSystem destruction;
    CharacterSystem character;
    LightingSystem lighting;
    AudioSystem audio;
    UISystem ui;
    NavigationSystem navigation;

    std::vector<ISystem*> systems = {
        &atmo, &particle, &water, &veg, &terrain,
        &cloth, &destruction, &character, &lighting,
        &audio, &ui, &navigation
    };

    for (ISystem* sys : systems)
    {
        EXPECT_FALSE(sys->getSystemName().empty());
        EXPECT_FALSE(sys->isActive());  // all start inactive
    }
}

TEST_F(DomainSystemTest, ForceActiveSystemsCorrectlyIdentified)
{
    AtmosphereSystem atmo;
    ParticleVfxSystem particle;
    WaterSystem water;
    VegetationSystem veg;
    TerrainSystem terrain;
    ClothSystem cloth;
    DestructionSystem destruction;
    CharacterSystem character;
    LightingSystem lighting;
    AudioSystem audio;
    UISystem ui;
    NavigationSystem navigation;

    // Only Atmosphere and Lighting are force-active
    EXPECT_TRUE(atmo.isForceActive());
    EXPECT_TRUE(lighting.isForceActive());

    EXPECT_FALSE(particle.isForceActive());
    EXPECT_FALSE(water.isForceActive());
    EXPECT_FALSE(veg.isForceActive());
    EXPECT_FALSE(terrain.isForceActive());
    EXPECT_FALSE(cloth.isForceActive());
    EXPECT_FALSE(destruction.isForceActive());
    EXPECT_FALSE(character.isForceActive());
    EXPECT_FALSE(audio.isForceActive());
    EXPECT_FALSE(ui.isForceActive());
    EXPECT_FALSE(navigation.isForceActive());
}
