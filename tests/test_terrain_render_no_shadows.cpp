// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_render_no_shadows.cpp
/// @brief 3D_E-0870 — terrain draws without a shadow map and raises no GL
///        error.
///
/// terrain.frag declares four sampler2DArrays: the shadow map and three
/// ground-material arrays. With no shadow map, or no ground materials, the
/// uniforms were left unset, so they read unit 0, where the heightmap (a
/// sampler2D) sits. Mesa then fails the draw with GL_INVALID_OPERATION
/// (renderer/sampler_fallback.h). Runs on the GL test context, which locally
/// and in CI is Mesa's software driver.
#include "environment/terrain.h"
#include "gl_test_fixture.h"
#include "renderer/camera.h"
#include "renderer/terrain_renderer.h"
#include "scene/scene.h"

#include <glad/gl.h>
#include <gtest/gtest.h>

#include <string>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

class TerrainRenderNoShadowsTest : public GLTestFixture {};

/// The assets directory: VESTIGE_SHADER_DIR is <source>/assets/shaders.
std::string assetDir()
{
    std::string shaders = VESTIGE_SHADER_DIR;
    return shaders.substr(0, shaders.rfind('/'));
}

} // namespace

TEST_F(TerrainRenderNoShadowsTest, SunWithoutShadowMapDrawsCleanly)
{
    TerrainConfig config;
    config.width  = 65;
    config.depth  = 65;
    config.maxLodLevels = 3;
    Terrain terrain;
    ASSERT_TRUE(terrain.initialize(config));

    TerrainRenderer renderer;
    ASSERT_TRUE(renderer.init(assetDir()));

    // Above the middle of the terrain, looking down so nodes are selected.
    Camera camera(glm::vec3(32.0f, 40.0f, 32.0f), -90.0f, -60.0f);

    SceneRenderData scene;
    scene.hasDirectionalLight = true;
    scene.directionalLight.direction = glm::normalize(glm::vec3(0.3f, -1.0f, 0.2f));

    while (glGetError() != GL_NO_ERROR) {}  // drop errors from before the draw
    renderer.render(terrain, camera, 1.0f, scene, /*csm=*/nullptr);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR))
        << "terrain drew with its shadow sampler unbound";

    EXPECT_GT(renderer.getLastDrawCallCount(), 0) << "the camera saw no terrain, so nothing was tested";

    renderer.shutdown();
    terrain.shutdown();
}
