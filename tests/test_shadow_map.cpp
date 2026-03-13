/// @file test_shadow_map.cpp
/// @brief Unit tests for the ShadowConfig struct and shadow map configuration.
#include "renderer/shadow_map.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

using namespace Vestige;

TEST(ShadowConfigTest, DefaultValues)
{
    ShadowConfig config;

    EXPECT_EQ(config.resolution, 2048);
    EXPECT_FLOAT_EQ(config.orthoSize, 15.0f);
    EXPECT_FLOAT_EQ(config.nearPlane, 1.0f);
    EXPECT_FLOAT_EQ(config.farPlane, 50.0f);
}

TEST(ShadowConfigTest, CustomValues)
{
    ShadowConfig config;
    config.resolution = 4096;
    config.orthoSize = 30.0f;
    config.nearPlane = 0.5f;
    config.farPlane = 100.0f;

    EXPECT_EQ(config.resolution, 4096);
    EXPECT_FLOAT_EQ(config.orthoSize, 30.0f);
    EXPECT_FLOAT_EQ(config.nearPlane, 0.5f);
    EXPECT_FLOAT_EQ(config.farPlane, 100.0f);
}

TEST(ShadowConfigTest, OrthoSizeCoversScene)
{
    // Verify default orthoSize covers our demo scene (ground plane is 20x20)
    ShadowConfig config;
    float sceneHalfSize = 10.0f;  // Ground plane extends -10 to +10
    EXPECT_GT(config.orthoSize, sceneHalfSize);
}

TEST(ShadowConfigTest, FarPlaneReachesScene)
{
    // Verify far plane is deep enough for our demo scene
    ShadowConfig config;
    EXPECT_GT(config.farPlane, 30.0f);  // Scene is roughly 20 units across
}
