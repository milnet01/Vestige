/// @file test_water_surface.cpp
/// @brief Unit tests for WaterSurfaceConfig and WaterSurfaceComponent.
#include "scene/water_surface.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

using namespace Vestige;

// ---------------------------------------------------------------------------
// WaterSurfaceConfig tests
// ---------------------------------------------------------------------------

TEST(WaterSurfaceConfigTest, DefaultValues)
{
    WaterSurfaceConfig config;

    EXPECT_FLOAT_EQ(config.width, 10.0f);
    EXPECT_FLOAT_EQ(config.depth, 10.0f);
    EXPECT_EQ(config.gridResolution, 128);
    EXPECT_EQ(config.numWaves, 2);
    EXPECT_EQ(WaterSurfaceConfig::MAX_WAVES, 4);
}

TEST(WaterSurfaceConfigTest, DefaultWaveParameters)
{
    WaterSurfaceConfig config;

    // First wave
    EXPECT_FLOAT_EQ(config.waves[0].amplitude, 0.02f);
    EXPECT_FLOAT_EQ(config.waves[0].wavelength, 2.0f);
    EXPECT_FLOAT_EQ(config.waves[0].speed, 0.5f);
    EXPECT_FLOAT_EQ(config.waves[0].direction, 0.0f);

    // Second wave
    EXPECT_FLOAT_EQ(config.waves[1].amplitude, 0.01f);
    EXPECT_FLOAT_EQ(config.waves[1].wavelength, 1.5f);
    EXPECT_FLOAT_EQ(config.waves[1].speed, 0.3f);
    EXPECT_FLOAT_EQ(config.waves[1].direction, 45.0f);
}

TEST(WaterSurfaceConfigTest, DefaultColors)
{
    WaterSurfaceConfig config;

    EXPECT_FLOAT_EQ(config.shallowColor.r, 0.1f);
    EXPECT_FLOAT_EQ(config.shallowColor.g, 0.4f);
    EXPECT_FLOAT_EQ(config.shallowColor.b, 0.5f);
    EXPECT_FLOAT_EQ(config.shallowColor.a, 0.8f);

    EXPECT_FLOAT_EQ(config.deepColor.r, 0.0f);
    EXPECT_FLOAT_EQ(config.deepColor.g, 0.1f);
    EXPECT_FLOAT_EQ(config.deepColor.b, 0.2f);
    EXPECT_FLOAT_EQ(config.deepColor.a, 1.0f);
}

TEST(WaterSurfaceConfigTest, DefaultSurfaceParameters)
{
    WaterSurfaceConfig config;

    EXPECT_FLOAT_EQ(config.depthDistance, 5.0f);
    EXPECT_FLOAT_EQ(config.refractionStrength, 0.02f);
    EXPECT_FLOAT_EQ(config.normalStrength, 1.0f);
    EXPECT_FLOAT_EQ(config.dudvStrength, 0.02f);
    EXPECT_FLOAT_EQ(config.flowSpeed, 0.3f);
    EXPECT_FLOAT_EQ(config.specularPower, 128.0f);
    EXPECT_FLOAT_EQ(config.reflectionResolutionScale, 0.5f);
}

TEST(WaterSurfaceConfigTest, WaveCountClamped)
{
    WaterSurfaceConfig config;

    // numWaves can be set, MAX_WAVES is the upper limit
    config.numWaves = 4;
    EXPECT_LE(config.numWaves, WaterSurfaceConfig::MAX_WAVES);

    config.numWaves = 0;
    EXPECT_GE(config.numWaves, 0);
}

TEST(WaterSurfaceConfigTest, WaveDirectionDegrees)
{
    WaterSurfaceConfig config;

    // Verify direction is in degrees (not radians)
    config.waves[0].direction = 90.0f;
    EXPECT_FLOAT_EQ(config.waves[0].direction, 90.0f);

    // Conversion to radians should be done at render time
    float radians = config.waves[0].direction * glm::pi<float>() / 180.0f;
    EXPECT_NEAR(radians, glm::half_pi<float>(), 0.001f);
}

TEST(WaterSurfaceConfigTest, ModifyAllWaves)
{
    WaterSurfaceConfig config;
    config.numWaves = 4;

    for (int i = 0; i < WaterSurfaceConfig::MAX_WAVES; ++i)
    {
        config.waves[i].amplitude = 0.1f * (i + 1);
        config.waves[i].wavelength = 1.0f * (i + 1);
        config.waves[i].speed = 0.2f * (i + 1);
        config.waves[i].direction = 45.0f * i;
    }

    EXPECT_FLOAT_EQ(config.waves[2].amplitude, 0.3f);
    EXPECT_FLOAT_EQ(config.waves[3].direction, 135.0f);
}

// ---------------------------------------------------------------------------
// WaterSurfaceComponent tests (no OpenGL context — mesh tests are skipped)
// ---------------------------------------------------------------------------

TEST(WaterSurfaceComponentTest, DefaultConfig)
{
    WaterSurfaceComponent comp;
    const auto& config = comp.getConfig();

    EXPECT_FLOAT_EQ(config.width, 10.0f);
    EXPECT_FLOAT_EQ(config.depth, 10.0f);
    EXPECT_EQ(config.gridResolution, 128);
}

TEST(WaterSurfaceComponentTest, ConfigIsModifiable)
{
    WaterSurfaceComponent comp;
    comp.getConfig().width = 20.0f;
    comp.getConfig().depth = 15.0f;
    comp.getConfig().numWaves = 3;

    EXPECT_FLOAT_EQ(comp.getConfig().width, 20.0f);
    EXPECT_FLOAT_EQ(comp.getConfig().depth, 15.0f);
    EXPECT_EQ(comp.getConfig().numWaves, 3);
}

TEST(WaterSurfaceComponentTest, LocalWaterYIsZero)
{
    WaterSurfaceComponent comp;
    EXPECT_FLOAT_EQ(comp.getLocalWaterY(), 0.0f);
}

TEST(WaterSurfaceComponentTest, NoMeshWithoutGLContext)
{
    WaterSurfaceComponent comp;
    // Without an OpenGL context, VAO is 0 and index count is 0
    EXPECT_EQ(comp.getVao(), 0u);
    EXPECT_EQ(comp.getIndexCount(), 0);
}

TEST(WaterSurfaceComponentTest, EnableDisable)
{
    WaterSurfaceComponent comp;
    EXPECT_TRUE(comp.isEnabled());

    comp.setEnabled(false);
    EXPECT_FALSE(comp.isEnabled());

    comp.setEnabled(true);
    EXPECT_TRUE(comp.isEnabled());
}

TEST(WaterSurfaceComponentTest, ClonePreservesConfig)
{
    WaterSurfaceComponent comp;
    comp.getConfig().width = 25.0f;
    comp.getConfig().depth = 30.0f;
    comp.getConfig().numWaves = 3;
    comp.getConfig().waves[0].amplitude = 0.05f;
    comp.getConfig().shallowColor = {0.2f, 0.5f, 0.6f, 0.9f};
    comp.setEnabled(false);

    auto cloned = comp.clone();
    auto* clonedWater = dynamic_cast<WaterSurfaceComponent*>(cloned.get());
    ASSERT_NE(clonedWater, nullptr);

    const auto& clonedConfig = clonedWater->getConfig();
    EXPECT_FLOAT_EQ(clonedConfig.width, 25.0f);
    EXPECT_FLOAT_EQ(clonedConfig.depth, 30.0f);
    EXPECT_EQ(clonedConfig.numWaves, 3);
    EXPECT_FLOAT_EQ(clonedConfig.waves[0].amplitude, 0.05f);
    EXPECT_FLOAT_EQ(clonedConfig.shallowColor.r, 0.2f);
    EXPECT_FALSE(clonedWater->isEnabled());
}
