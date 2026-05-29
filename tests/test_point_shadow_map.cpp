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

// ===========================================================================
// selectShadowCastersFromLights — Ts20-CV14 over-limit rejection
// ===========================================================================
//
// The renderer caps simultaneous point-shadow casters at
// MAX_POINT_SHADOW_LIGHTS (the shadow-map cubemap pool is fixed-size). The
// pure helper extracted from Renderer::selectShadowCastingPointLights() lets
// us verify the cap headlessly, including the MAX+1 case the prior audit
// (CV14) could not exercise without a GL context.

namespace
{
PointLight makeLight(bool castsShadow)
{
    PointLight light;
    light.castsShadow = castsShadow;
    return light;
}
}  // namespace

TEST(SelectShadowCastersTest, EmptyListSelectsNothing)
{
    std::vector<PointLight> lights;
    auto casters = selectShadowCastersFromLights(lights, MAX_POINT_SHADOW_LIGHTS);
    EXPECT_TRUE(casters.empty());
}

TEST(SelectShadowCastersTest, NonCastersAreSkipped)
{
    std::vector<PointLight> lights = {
        makeLight(false), makeLight(false), makeLight(false)
    };
    auto casters = selectShadowCastersFromLights(lights, MAX_POINT_SHADOW_LIGHTS);
    EXPECT_TRUE(casters.empty());
}

TEST(SelectShadowCastersTest, FewerThanMaxSelectsAll)
{
    // One caster, cap of 2 — selects the single caster.
    std::vector<PointLight> lights = { makeLight(true) };
    auto casters = selectShadowCastersFromLights(lights, MAX_POINT_SHADOW_LIGHTS);
    ASSERT_EQ(casters.size(), 1u);
    EXPECT_EQ(casters[0], 0);
}

TEST(SelectShadowCastersTest, ExactlyMaxSelectsAll)
{
    std::vector<PointLight> lights = { makeLight(true), makeLight(true) };
    auto casters = selectShadowCastersFromLights(lights, MAX_POINT_SHADOW_LIGHTS);
    ASSERT_EQ(casters.size(), static_cast<size_t>(MAX_POINT_SHADOW_LIGHTS));
    EXPECT_EQ(casters[0], 0);
    EXPECT_EQ(casters[1], 1);
}

TEST(SelectShadowCastersTest, OverLimitRejectsExtras_CV14)
{
    // The headline CV14 case: MAX+1 shadow-casting lights. Only the first
    // MAX_POINT_SHADOW_LIGHTS (by index) are selected; the surplus is dropped.
    std::vector<PointLight> lights;
    for (int i = 0; i < MAX_POINT_SHADOW_LIGHTS + 1; ++i)
    {
        lights.push_back(makeLight(true));
    }

    auto casters = selectShadowCastersFromLights(lights, MAX_POINT_SHADOW_LIGHTS);

    ASSERT_EQ(casters.size(), static_cast<size_t>(MAX_POINT_SHADOW_LIGHTS));
    // Lowest indices win; the (MAX+1)-th light is not present.
    for (int i = 0; i < MAX_POINT_SHADOW_LIGHTS; ++i)
    {
        EXPECT_EQ(casters[static_cast<size_t>(i)], i);
    }
}

TEST(SelectShadowCastersTest, SelectsLowestIndicesAmongInterleavedCasters)
{
    // Interleaved: non-caster, caster, caster, caster. With a cap of 2 the
    // first two casters (indices 1 and 2) win; index 3 is rejected.
    std::vector<PointLight> lights = {
        makeLight(false), makeLight(true), makeLight(true), makeLight(true)
    };

    auto casters = selectShadowCastersFromLights(lights, MAX_POINT_SHADOW_LIGHTS);

    ASSERT_EQ(casters.size(), 2u);
    EXPECT_EQ(casters[0], 1);
    EXPECT_EQ(casters[1], 2);
}

TEST(SelectShadowCastersTest, ZeroCapRejectsEverything)
{
    // Defensive: a cap of 0 selects nothing even when lights cast shadows.
    std::vector<PointLight> lights = { makeLight(true), makeLight(true) };
    auto casters = selectShadowCastersFromLights(lights, 0);
    EXPECT_TRUE(casters.empty());
}
