// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_brush.cpp
/// @brief 3D_E-0874 — the terrain brush is a circle in world metres, even when
///        the heightmap's texels are spaced differently along X and Z.

#include "gl_test_fixture.h"

#include "editor/command_history.h"
#include "editor/tools/terrain_brush.h"
#include "environment/terrain.h"

#include <gtest/gtest.h>

namespace Vestige
{
namespace
{

class TerrainBrushTest : public Test::GLTestFixture
{
};

TEST_F(TerrainBrushTest, BrushIsACircleWhenSpacingDiffersByAxis)
{
    TerrainConfig config;
    config.width = 33;
    config.depth = 33;
    config.spacingX = 1.0f;  // 1 m between texels along X
    config.spacingZ = 2.0f;  // 2 m between texels along Z
    Terrain terrain;
    ASSERT_TRUE(terrain.initialize(config));
    for (int z = 0; z < config.depth; ++z)
    {
        for (int x = 0; x < config.width; ++x)
        {
            terrain.setRawHeight(x, z, 0.0f);
        }
    }

    TerrainBrush brush;
    brush.setEnabled(true);
    brush.mode = TerrainBrushMode::RAISE;
    brush.radius = 4.0f;
    brush.falloff = 0.0f;  // sharp edge: every texel inside is raised

    // Straight down onto texel (16, 16), world (16 m, 32 m).
    Ray ray;
    ray.origin = glm::vec3(16.0f, 100.0f, 32.0f);
    ray.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    CommandHistory history;
    ASSERT_TRUE(brush.processInput(ray, true, 0.1f, terrain, history));

    EXPECT_GT(terrain.getRawHeight(16, 16), 0.0f) << "centre not raised";
    EXPECT_GT(terrain.getRawHeight(19, 16), 0.0f) << "3 m along X is inside 4 m";
    EXPECT_GT(terrain.getRawHeight(16, 17), 0.0f) << "2 m along Z is inside 4 m";
    EXPECT_FLOAT_EQ(terrain.getRawHeight(16, 19), 0.0f) << "6 m along Z is outside 4 m";
    EXPECT_FLOAT_EQ(terrain.getRawHeight(21, 16), 0.0f) << "5 m along X is outside 4 m";
}

} // namespace
} // namespace Vestige
