/// @file test_domain_systems.cpp
/// @brief Tests for Phase 9B domain system wrappers.
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
// Cross-system: all 9 systems have unique names
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

    EXPECT_EQ(names.size(), 9u);
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

    std::vector<ISystem*> systems = {
        &atmo, &particle, &water, &veg, &terrain,
        &cloth, &destruction, &character, &lighting
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
}
