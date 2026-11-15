// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_grass_renderer_gl_state.cpp
/// @brief GrassRenderer::render() leaves the caller's blend and cull state.
///
/// The blades draw opaque and two-sided, so the pass turns blending and
/// back-face culling off. The passes after it must find the state they left.
#include "environment/grass_config.h"
#include "environment/terrain.h"
#include "gl_test_fixture.h"
#include "lsan_guard.h"
#include "renderer/grass_renderer.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <string>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

class GrassRendererGlState : public GLTestFixture
{
protected:
    void TearDown() override
    {
        if (!Vestige::Test::GLTestEnvironment::wasInitialized())
        {
            return;  // skipped: there are no GL functions to call
        }
        // The GL context is shared by every test: leave GL's defaults.
        glDisable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ZERO);
        glDisable(GL_CULL_FACE);
    }
};

/// The assets directory: VESTIGE_SHADER_DIR is <source>/assets/shaders.
std::string assetDir()
{
    std::string shaders = VESTIGE_SHADER_DIR;
    return shaders.substr(0, shaders.rfind('/'));
}

GLint getInt(GLenum name)
{
    GLint value = 0;
    glGetIntegerv(name, &value);
    return value;
}

} // namespace

// 3D_E-0874 lane-05: render() turned blending and culling off and left them off.
TEST_F(GrassRendererGlState, RenderLeavesTheCallersBlendAndCullState)
{
    TerrainConfig terrainConfig;
    terrainConfig.width = 65;
    terrainConfig.depth = 65;
    terrainConfig.maxLodLevels = 3;
    Terrain terrain;
    ASSERT_TRUE(terrain.initialize(terrainConfig));

    GrassRenderer grass;
    ASSERT_TRUE(grass.init(assetDir()));
    grass.buildField(terrain, GrassConfig{});
    ASSERT_TRUE(grass.hasField()) << "no blades were placed, so nothing was drawn";

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glEnable(GL_CULL_FACE);

    // Above the middle of the terrain, looking down across it.
    const glm::vec3 eye(32.0f, 6.0f, 20.0f);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(32.0f, 0.0f, 40.0f), glm::vec3(0, 1, 0));
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 200.0f);
    {
        // llvmpipe keeps process-lifetime pipe state from the first draw: not ours.
        Vestige::Test::ScopedLeakCheckDisable noLeakTracking;
        grass.render(projection * view, view, eye, 0.0f, nullptr, nullptr);
    }
    ASSERT_GT(grass.drawnChunkCount(), 0u) << "the camera saw no grass, so nothing was tested";

    EXPECT_EQ(glIsEnabled(GL_BLEND), GL_TRUE) << "blending left off";
    EXPECT_EQ(glIsEnabled(GL_CULL_FACE), GL_TRUE) << "culling left off";
    EXPECT_EQ(getInt(GL_BLEND_SRC_RGB), GL_ONE);
    EXPECT_EQ(getInt(GL_BLEND_DST_RGB), GL_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(getInt(GL_BLEND_SRC_ALPHA), GL_ZERO);
    EXPECT_EQ(getInt(GL_BLEND_DST_ALPHA), GL_ONE);

    grass.shutdown();
    terrain.shutdown();
}
