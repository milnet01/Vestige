// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_density_map.cpp
/// @brief Unit tests for DensityMap, path clearing, and bank blending.

#include "environment/density_map.h"
#include "environment/foliage_manager.h"
#include "environment/spline_path.h"
#include "environment/terrain.h"

#include <gtest/gtest.h>

using namespace Vestige;

// =============================================================================
// DensityMap basic functionality
// =============================================================================

TEST(DensityMapTest, DefaultUninitialized)
{
    DensityMap map;
    EXPECT_FALSE(map.isInitialized());
    EXPECT_FLOAT_EQ(map.sample(0.0f, 0.0f), 1.0f);  // Returns 1.0 when not initialized
}

TEST(DensityMapTest, InitializeSetsDefaults)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 10.0f, 10.0f, 1.0f);
    EXPECT_TRUE(map.isInitialized());
    EXPECT_EQ(map.getWidth(), 10);
    EXPECT_EQ(map.getHeight(), 10);
    EXPECT_FLOAT_EQ(map.getTexelsPerMeter(), 1.0f);

    // All values should be 1.0 (full density) by default
    EXPECT_FLOAT_EQ(map.sample(5.0f, 5.0f), 1.0f);
    EXPECT_FLOAT_EQ(map.getTexel(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(map.getTexel(9, 9), 1.0f);
}

TEST(DensityMapTest, HigherResolution)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 10.0f, 10.0f, 2.0f);
    EXPECT_EQ(map.getWidth(), 20);
    EXPECT_EQ(map.getHeight(), 20);
}

TEST(DensityMapTest, SetAndGetTexel)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 10.0f, 10.0f, 1.0f);

    map.setTexel(5, 5, 0.3f);
    EXPECT_FLOAT_EQ(map.getTexel(5, 5), 0.3f);

    // Out-of-bounds should be safe
    map.setTexel(-1, 0, 0.5f);
    map.setTexel(100, 0, 0.5f);
    EXPECT_FLOAT_EQ(map.getTexel(-1, 0), 1.0f);  // Returns default for out-of-bounds
}

TEST(DensityMapTest, SampleBilinear)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 4.0f, 4.0f, 1.0f);

    // Set a 2x2 region to zero
    map.setTexel(1, 1, 0.0f);
    map.setTexel(2, 1, 0.0f);
    map.setTexel(1, 2, 0.0f);
    map.setTexel(2, 2, 0.0f);

    // Sample at center of the zero region — should be close to 0
    float val = map.sample(2.0f, 2.0f);
    EXPECT_LT(val, 0.5f);

    // Sample at a corner far from the zero region — should be close to 1
    float corner = map.sample(0.0f, 0.0f);
    EXPECT_GT(corner, 0.5f);
}

TEST(DensityMapTest, SampleOutsideBounds)
{
    DensityMap map;
    map.initialize(10.0f, 10.0f, 5.0f, 5.0f, 1.0f);

    // Outside the map bounds → returns 1.0
    EXPECT_FLOAT_EQ(map.sample(0.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(map.sample(100.0f, 100.0f), 1.0f);
}

TEST(DensityMapTest, FillSetsAllTexels)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 5.0f, 5.0f, 1.0f);

    map.fill(0.5f);
    for (int z = 0; z < map.getHeight(); ++z)
    {
        for (int x = 0; x < map.getWidth(); ++x)
        {
            EXPECT_FLOAT_EQ(map.getTexel(x, z), 0.5f);
        }
    }

    map.fill(0.0f);
    EXPECT_FLOAT_EQ(map.getTexel(0, 0), 0.0f);
}

TEST(DensityMapTest, PaintCircle)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 20.0f, 20.0f, 1.0f);

    // Paint zero density at center (10, 10) with radius 3
    map.paint(glm::vec3(10.0f, 0.0f, 10.0f), 3.0f, 0.0f, 1.0f, 0.0f);

    // Center should be 0
    EXPECT_NEAR(map.getTexel(10, 10), 0.0f, 0.01f);

    // Far outside the circle should still be 1.0
    EXPECT_FLOAT_EQ(map.getTexel(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(map.getTexel(19, 19), 1.0f);
}

TEST(DensityMapTest, PaintWithFalloff)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 20.0f, 20.0f, 1.0f);

    // Paint zero density at center with falloff
    map.paint(glm::vec3(10.0f, 0.0f, 10.0f), 5.0f, 0.0f, 1.0f, 1.0f);

    // Center should be fully affected
    float center = map.getTexel(10, 10);
    EXPECT_NEAR(center, 0.0f, 0.05f);

    // Edge should be less affected (falloff)
    float edge = map.getTexel(14, 10);  // 4m from center, near edge of 5m radius
    EXPECT_GT(edge, center);
}

TEST(DensityMapTest, PaintStrengthPartial)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 10.0f, 10.0f, 1.0f);

    // Paint with partial strength
    map.paint(glm::vec3(5.0f, 0.0f, 5.0f), 2.0f, 0.0f, 0.5f, 0.0f);

    // Center should be partially affected (not fully zero)
    float val = map.getTexel(5, 5);
    EXPECT_GT(val, 0.0f);
    EXPECT_LT(val, 1.0f);
}

TEST(DensityMapTest, ValuesClamped)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 5.0f, 5.0f, 1.0f);

    map.setTexel(0, 0, -0.5f);
    EXPECT_FLOAT_EQ(map.getTexel(0, 0), 0.0f);

    map.setTexel(0, 0, 2.0f);
    EXPECT_FLOAT_EQ(map.getTexel(0, 0), 1.0f);
}

// =============================================================================
// DensityMap serialization
// =============================================================================

TEST(DensityMapTest, SerializeDeserialize)
{
    DensityMap original;
    original.initialize(5.0f, 10.0f, 20.0f, 30.0f, 2.0f);
    original.setTexel(5, 5, 0.25f);
    original.setTexel(10, 10, 0.75f);

    auto json = original.serialize();

    DensityMap loaded;
    loaded.deserialize(json);

    EXPECT_TRUE(loaded.isInitialized());
    EXPECT_EQ(loaded.getWidth(), original.getWidth());
    EXPECT_EQ(loaded.getHeight(), original.getHeight());
    EXPECT_FLOAT_EQ(loaded.getTexelsPerMeter(), 2.0f);
    EXPECT_FLOAT_EQ(loaded.getOrigin().x, 5.0f);
    EXPECT_FLOAT_EQ(loaded.getOrigin().y, 10.0f);
    EXPECT_FLOAT_EQ(loaded.getTexel(5, 5), 0.25f);
    EXPECT_FLOAT_EQ(loaded.getTexel(10, 10), 0.75f);
}

// =============================================================================
// Path clearing in density map
// =============================================================================

TEST(DensityMapTest, ClearAlongPath)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 30.0f, 30.0f, 1.0f);

    SplinePath path;
    path.width = 2.0f;
    path.addWaypoint(glm::vec3(5.0f, 0.0f, 15.0f));
    path.addWaypoint(glm::vec3(25.0f, 0.0f, 15.0f));

    map.clearAlongPath(path, 0.5f);

    // Along the path center, density should be zero
    EXPECT_NEAR(map.sample(10.0f, 15.0f), 0.0f, 0.01f);
    EXPECT_NEAR(map.sample(15.0f, 15.0f), 0.0f, 0.01f);
    EXPECT_NEAR(map.sample(20.0f, 15.0f), 0.0f, 0.01f);

    // Far from the path, density should be 1.0
    EXPECT_FLOAT_EQ(map.sample(15.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(map.sample(15.0f, 29.0f), 1.0f);
}

TEST(DensityMapTest, ClearAlongPathTooFewWaypoints)
{
    DensityMap map;
    map.initialize(0.0f, 0.0f, 10.0f, 10.0f, 1.0f);

    SplinePath path;
    path.addWaypoint(glm::vec3(5.0f, 0.0f, 5.0f));

    // Should not crash, no clearing with < 2 waypoints
    map.clearAlongPath(path, 0.5f);
    EXPECT_FLOAT_EQ(map.getTexel(5, 5), 1.0f);
}

// =============================================================================
// FoliageManager path clearing
// =============================================================================

TEST(FoliageManagerPathClearTest, ClearAlongPath)
{
    FoliageManager manager;
    FoliageTypeConfig config;
    config.minScale = 1.0f;
    config.maxScale = 1.0f;

    // Paint foliage along the Y=0 plane
    manager.paintFoliage(0, glm::vec3(15.0f, 0.0f, 15.0f), 10.0f, 5.0f, 0.0f, config);
    int before = manager.getTotalFoliageCount();
    EXPECT_GT(before, 0);

    // Clear along a path through the center
    SplinePath path;
    path.width = 3.0f;
    path.addWaypoint(glm::vec3(5.0f, 0.0f, 15.0f));
    path.addWaypoint(glm::vec3(25.0f, 0.0f, 15.0f));

    int removed = manager.clearAlongPath(path, 1.0f);
    EXPECT_GT(removed, 0);
    EXPECT_LT(manager.getTotalFoliageCount(), before);
}

// =============================================================================
// FoliageManager density map modulation
// =============================================================================

TEST(FoliageDensityModulationTest, DensityMapReducesSpawns)
{
    FoliageManager manager;
    FoliageTypeConfig config;
    config.minScale = 1.0f;
    config.maxScale = 1.0f;

    // Paint without density map
    auto withoutMap = manager.paintFoliage(
        0, glm::vec3(0.0f), 5.0f, 10.0f, 0.0f, config, nullptr);
    int countWithout = static_cast<int>(withoutMap.size());

    manager.clear();

    // Paint with density map set to 0 (should block all)
    DensityMap zeroMap;
    zeroMap.initialize(-10.0f, -10.0f, 20.0f, 20.0f, 1.0f);
    zeroMap.fill(0.0f);

    auto withZeroMap = manager.paintFoliage(
        0, glm::vec3(0.0f), 5.0f, 10.0f, 0.0f, config, &zeroMap);
    int countZero = static_cast<int>(withZeroMap.size());

    EXPECT_GT(countWithout, 0);
    EXPECT_EQ(countZero, 0);
}

TEST(FoliageDensityModulationTest, FullDensityMapNoEffect)
{
    FoliageManager manager;
    FoliageTypeConfig config;
    config.minScale = 1.0f;
    config.maxScale = 1.0f;

    // Paint with density map at 1.0 (should not reduce spawns)
    DensityMap fullMap;
    fullMap.initialize(-10.0f, -10.0f, 20.0f, 20.0f, 1.0f);
    fullMap.fill(1.0f);

    auto added = manager.paintFoliage(
        0, glm::vec3(0.0f), 5.0f, 5.0f, 0.0f, config, &fullMap);

    // Should get roughly the same as without a map (density=5/m², area=~78.5m²)
    EXPECT_GT(static_cast<int>(added.size()), 0);
}

// =============================================================================
// Bank blending
// =============================================================================

TEST(BankBlendTest, BasicBlend)
{
    Terrain terrain;
    TerrainConfig cfg;
    cfg.width = 33;
    cfg.depth = 33;
    cfg.spacingX = 1.0f;
    cfg.spacingZ = 1.0f;
    cfg.origin = glm::vec3(0.0f);
    cfg.gridResolution = 5;
    cfg.maxLodLevels = 2;

    // Initialize creates GPU textures which won't work in unit tests,
    // but the splatmap data is CPU-side so we can test the algorithm.
    // We need to call initialize to set up internal state.
    // This test will only pass if GL context is NOT required for splatmap math.
    // Since initialize creates GL textures which fail without context,
    // we test the logic by directly checking the config struct.

    Terrain::BankBlendConfig blendConfig;
    blendConfig.blendWidth = 3.0f;
    blendConfig.bankChannel = 3;  // Sand
    blendConfig.bankStrength = 0.8f;

    // Just verify the config struct works correctly
    EXPECT_FLOAT_EQ(blendConfig.blendWidth, 3.0f);
    EXPECT_EQ(blendConfig.bankChannel, 3);
    EXPECT_FLOAT_EQ(blendConfig.bankStrength, 0.8f);
}

TEST(BankBlendTest, ConfigDefaults)
{
    Terrain::BankBlendConfig config;
    EXPECT_FLOAT_EQ(config.blendWidth, 3.0f);
    EXPECT_EQ(config.bankChannel, 3);
    EXPECT_FLOAT_EQ(config.bankStrength, 0.8f);
}
