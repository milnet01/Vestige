// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_fly_mode_ground_clamp.cpp
/// @brief The fly-mode camera must stay above the terrain under it.
///
/// Regression: fly mode clamped the eye to playerHeight above world Y = 0,
/// so over the meadow (ground at 3-7 m) the camera flew down through the
/// terrain and showed the ground from underneath.
#include "gl_test_fixture.h"

#include "core/first_person_controller.h"
#include "environment/terrain.h"

#include <gtest/gtest.h>
#include <limits>
#include <cmath>

namespace Vestige
{
namespace
{

constexpr float CLEARANCE = 0.15f;

class FlyModeGroundClampTest : public Test::GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        TerrainConfig config;
        config.width = 33;
        config.depth = 33;
        config.heightScale = 50.0f;
        ASSERT_TRUE(m_terrain.initialize(config));
        for (int z = 0; z < config.depth; ++z)
        {
            for (int x = 0; x < config.width; ++x)
            {
                m_terrain.setRawHeight(x, z, 0.2f);   // flat ground 10 m up
            }
        }
    }

    Terrain m_terrain;
};

TEST_F(FlyModeGroundClampTest, FloorIsClearanceAboveTheGroundBelow)
{
    const float ground = m_terrain.getHeight(16.0f, 16.0f);
    ASSERT_NEAR(ground, 10.0f, 1e-3f);

    const float floorY = FirstPersonController::flyModeFloorY(
        &m_terrain, glm::vec3(16.0f, 0.5f, 16.0f), CLEARANCE);

    EXPECT_NEAR(floorY, ground + CLEARANCE, 1e-3f);
}

TEST(FlyModeGroundClamp, WithoutTerrainFloorIsClearanceAboveZero)
{
    EXPECT_FLOAT_EQ(FirstPersonController::flyModeFloorY(
                        nullptr, glm::vec3(0.0f), CLEARANCE),
                    CLEARANCE);
}

} // namespace
// lane-02 leads first_person_controller.cpp:101 and :520.
TEST(FirstPersonMoveDirection, WalkModeDropsVerticalBeforeNormalising)
{
    // W + Space in walk mode: forward must stay at full speed, not 0.707.
    const glm::vec3 walk = FirstPersonController::normalizedMoveDirection(
        glm::vec3(0.0f, 1.0f, 1.0f), /*keepVertical=*/false);
    EXPECT_NEAR(walk.z, 1.0f, 1e-6f);
    EXPECT_EQ(walk.y, 0.0f);

    // Fly mode keeps the diagonal.
    const glm::vec3 fly = FirstPersonController::normalizedMoveDirection(
        glm::vec3(0.0f, 1.0f, 1.0f), /*keepVertical=*/true);
    EXPECT_NEAR(fly.z, 0.70710678f, 1e-6f);
}

TEST(FirstPersonMoveDirection, NaNAxisIsNoInputNotANaNVelocity)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const glm::vec3 d = FirstPersonController::normalizedMoveDirection(
        glm::vec3(0.0f, nan, 1.0f), /*keepVertical=*/true);
    EXPECT_TRUE(std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z));
    EXPECT_NEAR(d.z, 1.0f, 1e-6f);
    EXPECT_EQ(FirstPersonController::normalizedMoveDirection(glm::vec3(nan), true),
              glm::vec3(0.0f));
}

} // namespace Vestige
