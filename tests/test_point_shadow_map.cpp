// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_point_shadow_map.cpp
/// @brief Unit tests for PointShadowConfig and castsShadow flag.
#include "renderer/point_shadow_map.h"
#include "renderer/light.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(PointShadowConfigTest, DefaultValues)
{
    PointShadowConfig config;

    EXPECT_EQ(config.resolution, 1024);
    EXPECT_FLOAT_EQ(config.nearPlane, 0.1f);
    EXPECT_FLOAT_EQ(config.farPlane, 25.0f);
}

TEST(PointShadowConfigTest, CustomValues)
{
    PointShadowConfig config;
    config.resolution = 2048;
    config.nearPlane = 0.5f;
    config.farPlane = 50.0f;

    EXPECT_EQ(config.resolution, 2048);
    EXPECT_FLOAT_EQ(config.nearPlane, 0.5f);
    EXPECT_FLOAT_EQ(config.farPlane, 50.0f);
}

TEST(PointShadowConfigTest, MaxPointShadowLightsConstant)
{
    EXPECT_EQ(MAX_POINT_SHADOW_LIGHTS, 2);
}

TEST(PointLightCastsShadowTest, DefaultFalse)
{
    PointLight light;
    EXPECT_FALSE(light.castsShadow);
}

TEST(PointLightCastsShadowTest, CanEnable)
{
    PointLight light;
    light.castsShadow = true;
    EXPECT_TRUE(light.castsShadow);
}

TEST(PointShadowConfigTest, FarPlaneCoversScene)
{
    // Default far plane should cover the demo scene (cubes within ~10 units)
    PointShadowConfig config;
    EXPECT_GT(config.farPlane, 10.0f);
}
