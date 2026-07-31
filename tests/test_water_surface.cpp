// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_water_surface.cpp
/// @brief Unit tests for WaterSurfaceConfig and WaterSurfaceComponent.
///
/// @note Tests run without an active GL context — any VAO/buffer/mesh upload
///       operations on WaterSurfaceComponent are skipped or short-circuit on a
///       null context. Only CPU-side state (config defaults, animation phase,
///       displacement math) is exercised here.
#include "scene/water_surface.h"
#include "utils/frustum.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
    EXPECT_FLOAT_EQ(config.reflectionResolutionScale, 0.25f);
    EXPECT_EQ(config.reflectionMode, WaterReflectionMode::PLANAR);
    EXPECT_TRUE(config.refractionEnabled);

    // Slice 18 Ts2: previously-untested caustic + qualityTier defaults.
    EXPECT_TRUE(config.causticsEnabled);
    EXPECT_FLOAT_EQ(config.causticsIntensity, 0.15f);
    EXPECT_FLOAT_EQ(config.causticsScale, 0.1f);
    EXPECT_EQ(config.qualityTier, 0);
}

// Slice 18 Ts1 cleanup: renamed from `WaveCountClamped` — the body
// never attempts an out-of-range write, so the clamp half of the
// contract is unpinned. `numWaves` is a plain public int with no
// runtime clamp; this test verifies only the in-range round-trip.
TEST(WaterSurfaceConfigTest, WaveCountInRangeRoundTrips)
{
    WaterSurfaceConfig config;

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

// ---------------------------------------------------------------------------
// waterSurfaceWorldBounds + reflection-pass culling (3D_E-0028)
// ---------------------------------------------------------------------------
//
// The reflection and refraction passes each re-render the whole scene into an
// off-screen FBO, so the render loop skips both when no water surface lies in
// the camera frustum. These lock the two halves of that decision: the bounds
// must cover the mesh the component actually builds, and the frustum test must
// reject a pond behind the camera while accepting one in front.

TEST(WaterSurfaceBoundsTest, CoversMeshExtentAtIdentity)
{
    WaterSurfaceConfig config;   // 10 x 10, 2 waves @ 0.02 + 0.01 amplitude
    const AABB bounds = waterSurfaceWorldBounds(config, glm::mat4(1.0f));

    // Spans the full mesh footprint: buildMesh lays vertices over +/- width/2
    // in X and +/- depth/2 in Z.
    EXPECT_FLOAT_EQ(bounds.min.x, -5.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 5.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, -5.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, 5.0f);

    // Vertically inflated by the summed wave crest plus the fixed margin, so a
    // surface is never culled while a wave peak could still rasterise.
    EXPECT_GT(bounds.max.y, 0.03f);
    EXPECT_LT(bounds.min.y, -0.03f);
    EXPECT_FLOAT_EQ(bounds.max.y, -bounds.min.y);
}

TEST(WaterSurfaceBoundsTest, FollowsWorldTransform)
{
    WaterSurfaceConfig config;
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 7.0f, -40.0f));
    const AABB bounds = waterSurfaceWorldBounds(config, world);

    EXPECT_NEAR(bounds.getCenter().x, 100.0f, 1e-4f);
    EXPECT_NEAR(bounds.getCenter().y, 7.0f, 1e-4f);
    EXPECT_NEAR(bounds.getCenter().z, -40.0f, 1e-4f);
}

TEST(WaterSurfaceBoundsTest, ClampsOutOfRangeWaveCount)
{
    // numWaves is a plain int on a serialized config, so a bad scene file can
    // put it outside [0, MAX_WAVES]. Neither extreme may read past the array.
    WaterSurfaceConfig tooMany;
    tooMany.numWaves = 99;
    const AABB high = waterSurfaceWorldBounds(tooMany, glm::mat4(1.0f));

    WaterSurfaceConfig negative;
    negative.numWaves = -3;
    const AABB low = waterSurfaceWorldBounds(negative, glm::mat4(1.0f));

    EXPECT_GT(high.max.y, 0.0f);
    EXPECT_GT(low.max.y, 0.0f);
    EXPECT_GE(high.max.y, low.max.y);
}

TEST(WaterSurfaceCullTest, PondInFrontIsVisibleAndPondBehindIsCulled)
{
    // Mirrors the render loop's gate: a 20 x 20 pond 30 m along -Z, a camera at
    // the origin. Facing the pond it must render; turned 180 degrees away it
    // must be culled -- that turn is what the open_meadow visual-test viewpoint
    // exercises in-engine.
    WaterSurfaceConfig config;
    config.width = 20.0f;
    config.depth = 20.0f;
    const glm::mat4 pondWorld =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -30.0f));
    const AABB pond = waterSurfaceWorldBounds(config, pondWorld);

    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);

    const glm::mat4 lookAtPond =
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(isAabbInFrustum(pond, extractFrustumPlanes(proj * lookAtPond)));

    const glm::mat4 lookAway =
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FALSE(isAabbInFrustum(pond, extractFrustumPlanes(proj * lookAway)));
}

TEST(WaterSurfaceCullTest, PondBeyondFarPlaneIsCulled)
{
    WaterSurfaceConfig config;
    config.width = 20.0f;
    config.depth = 20.0f;
    const AABB farPond = waterSurfaceWorldBounds(
        config, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -4000.0f)));

    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);
    const glm::mat4 view =
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    EXPECT_FALSE(isAabbInFrustum(farPond, extractFrustumPlanes(proj * view)));
}
