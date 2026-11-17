// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_skybox_exposure.cpp
/// @brief 3D_E-0874 lane 14 — the HDRI viewer's exposure slider brightens
///        the skybox preview. skybox.frag had no exposure input, so the
///        slider changed nothing there.
///
/// The main renderer shares skybox.frag and never sets the exposure, so an
/// unset exposure must leave the sky unchanged.
#include "gl_test_fixture.h"
#include "lsan_guard.h"
#include "renderer/ibl_prefilter.h"
#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <string>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

constexpr int kRes = 4;
constexpr float kSky = 0.25f;

class SkyboxExposureTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        const std::string dir = VESTIGE_SHADER_DIR;
        ASSERT_TRUE(m_shader.loadFromFiles(dir + "/skybox.vert.glsl", dir + "/skybox.frag.glsl"));
        createIblCubeMesh(m_cubeVao, m_cubeVbo);

        // A uniformly grey sky.
        glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_cubemap);
        glTextureStorage2D(m_cubemap, 1, GL_RGBA16F, 1, 1);
        const std::array<float, 4> grey = {kSky, kSky, kSky, 1.0f};
        for (int face = 0; face < 6; ++face)
        {
            glTextureSubImage3D(m_cubemap, 0, 0, 0, face, 1, 1, 1, GL_RGBA, GL_FLOAT, grey.data());
        }

        glCreateTextures(GL_TEXTURE_2D, 1, &m_target);
        glTextureStorage2D(m_target, 1, GL_RGBA16F, kRes, kRes);
        glCreateFramebuffers(1, &m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_target, 0);
    }

    void TearDown() override
    {
        if (IsSkipped())
        {
            return;
        }
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteTextures(1, &m_target);
        glDeleteTextures(1, &m_cubemap);
        glDeleteBuffers(1, &m_cubeVbo);
        glDeleteVertexArrays(1, &m_cubeVao);
        GLTestFixture::TearDown();
    }

    /// Draws the sky looking down +X and returns the centre texel's red.
    float drawSky()
    {
        Vestige::Test::ScopedLeakCheckDisable noLeakTracking;
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, kRes, kRes);
        glDisable(GL_DEPTH_TEST);
        const GLboolean cullWasOn = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);  // the camera sits inside the cube; runIblFacePass leaves culling on
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        m_shader.use();
        m_shader.setMat4("u_view", iblCaptureViews()[0]);
        m_shader.setMat4("u_projection", iblCaptureProjection());
        m_shader.setBool("u_hasCubemap", true);
        glBindTextureUnit(0, m_cubemap);
        m_shader.setInt("u_skyboxTexture", 0);
        drawIblCube(m_cubeVao);

        std::array<float, 4> texel = {};
        glGetTextureSubImage(m_target, 0, kRes / 2, kRes / 2, 0, 1, 1, 1, GL_RGBA, GL_FLOAT,
                             static_cast<GLsizei>(sizeof(texel)), texel.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (cullWasOn == GL_TRUE)
        {
            glEnable(GL_CULL_FACE);
        }
        return texel[0];
    }

    Shader m_shader;
    GLuint m_cubeVao = 0;
    GLuint m_cubeVbo = 0;
    GLuint m_cubemap = 0;
    GLuint m_target = 0;
    GLuint m_fbo = 0;
};

} // namespace

TEST_F(SkyboxExposureTest, UnsetExposureLeavesTheSkyUnchanged)
{
    EXPECT_FLOAT_EQ(drawSky(), kSky);
}

TEST_F(SkyboxExposureTest, OneStopDoublesTheSky)
{
    m_shader.use();
    m_shader.setFloat("u_exposure", 1.0f);
    EXPECT_FLOAT_EQ(drawSky(), 2.0f * kSky);
}

TEST_F(SkyboxExposureTest, MinusOneStopHalvesTheSky)
{
    m_shader.use();
    m_shader.setFloat("u_exposure", -1.0f);
    EXPECT_FLOAT_EQ(drawSky(), 0.5f * kSky);
}
