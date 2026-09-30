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

TEST_F(DomainSystemTest, AtmosphereSystemStartsInactive)
{
    AtmosphereSystem sys;
    EXPECT_FALSE(sys.isActive());
}

TEST_F(DomainSystemTest, AtmosphereSystemHasEnvironmentForces)
{
    AtmosphereSystem sys;
    // EnvironmentForces is returned by reference (can't be null). Assert an
    // observable default — a freshly constructed subsystem reports no
    // precipitation — rather than void-discarding the accessor, so the test
    // fails if the subsystem ever hands back a mis-initialised object.
    EXPECT_FLOAT_EQ(sys.getEnvironmentForces().getPrecipitationIntensity(), 0.0f);
}

// ==========================================================================
// ParticleVfxSystem
// ==========================================================================

TEST_F(DomainSystemTest, ParticleSystemName)
{
    ParticleVfxSystem sys;
    EXPECT_EQ(sys.getSystemName(), "ParticleVFX");
}

TEST_F(DomainSystemTest, ParticleSystemHasRenderer)
{
    ParticleVfxSystem sys;
    (void)sys.getParticleRenderer();  // reference accessor — see Atmosphere note
}

// ==========================================================================
// WaterSystem
// ==========================================================================

TEST_F(DomainSystemTest, WaterSystemName)
{
    WaterSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Water");
}

TEST_F(DomainSystemTest, WaterSystemHasRendererAndFbo)
{
    WaterSystem sys;
    (void)sys.getWaterRenderer();  // reference accessor — see Atmosphere note
    (void)sys.getWaterFbo();
}

// ==========================================================================
// VegetationSystem
// ==========================================================================

TEST_F(DomainSystemTest, VegetationSystemName)
{
    VegetationSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Vegetation");
}

TEST_F(DomainSystemTest, VegetationSystemHasSubsystems)
{
    VegetationSystem sys;
    (void)sys.getFoliageManager();  // reference accessor — see Atmosphere note
    (void)sys.getFoliageRenderer();
    (void)sys.getTreeRenderer();
}

// ==========================================================================
// TerrainSystem
// ==========================================================================

TEST_F(DomainSystemTest, TerrainSystemName)
{
    TerrainSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Terrain");
}

TEST_F(DomainSystemTest, TerrainSystemHasSubsystems)
{
    TerrainSystem sys;
    (void)sys.getTerrain();  // reference accessor — see Atmosphere note
    (void)sys.getTerrainRenderer();
}

// ==========================================================================
// ClothSystem
// ==========================================================================

TEST_F(DomainSystemTest, ClothSystemName)
{
    ClothSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Cloth");
}

// ==========================================================================
// DestructionSystem
// ==========================================================================

TEST_F(DomainSystemTest, DestructionSystemName)
{
    DestructionSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Destruction");
}

// ==========================================================================
// CharacterSystem
// ==========================================================================

TEST_F(DomainSystemTest, CharacterSystemName)
{
    CharacterSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Character");
}

TEST_F(DomainSystemTest, CharacterSystemHasController)
{
    CharacterSystem sys;
    (void)sys.getPhysicsCharController();  // reference accessor — see Atmosphere note
}

// ==========================================================================
// LightingSystem
// ==========================================================================

TEST_F(DomainSystemTest, LightingSystemName)
{
    LightingSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Lighting");
}

// ==========================================================================
// AudioSystem
// ==========================================================================

TEST_F(DomainSystemTest, AudioSystemName)
{
    AudioSystem sys;
    EXPECT_EQ(sys.getSystemName(), "Audio");
}

TEST_F(DomainSystemTest, AudioSystemHasAudioEngine)
{
    AudioSystem sys;
    (void)sys.getAudioEngine();  // reference accessor — see Atmosphere note
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
