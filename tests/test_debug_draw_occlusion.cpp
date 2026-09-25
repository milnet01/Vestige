// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_debug_draw_occlusion.cpp
/// @brief DebugDraw lines are hidden behind scene depth (3D_E-0699).
///
/// The editor draws debug lines (the ground grid, gizmos) into its LDR
/// output FBO, whose depth buffer never holds scene depth. Before the fix
/// every line drew over everything: the 100 m ground grid at y = 0.03
/// showed through terrain and grass as a striped band. `DebugDraw::flush`
/// now takes the resolved scene depth and the line shader discards a
/// fragment the scene is in front of.
///
/// Setup: an 8x8 colour target with depth testing OFF, so only the shader
/// can hide a line. A reverse-Z scene depth texture holds 0.5 everywhere
/// (larger = nearer, as the engine's glClipControl(ZERO_TO_ONE) + GEQUAL
/// convention has it). With an identity view-projection a vertex's z is
/// its window depth, so a line at z 0.8 is in front of the scene and a
/// line at z 0.2 is behind it.

#include "gl_test_fixture.h"
#include "renderer/debug_draw.h"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <vector>

namespace Vestige::Test
{

namespace
{

constexpr int SIZE = 8;

// Pixel rows 2 and 5, hit at their centres so rasterisation is unambiguous.
constexpr int FRONT_ROW = 2;
constexpr int BACK_ROW = 5;
float rowToNdc(int row) { return (((static_cast<float>(row) + 0.5f) / SIZE) * 2.0f) - 1.0f; }

class DebugDrawOcclusionTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }

        const std::filesystem::path assets =
            std::filesystem::path(VESTIGE_SHADER_DIR).parent_path();
        ASSERT_TRUE(m_debugDraw.initialize(assets.string()));

        glGetIntegerv(GL_CLIP_ORIGIN, &m_prevClipOrigin);
        glGetIntegerv(GL_CLIP_DEPTH_MODE, &m_prevClipDepth);
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

        glCreateTextures(GL_TEXTURE_2D, 1, &m_color);
        glTextureStorage2D(m_color, 1, GL_RGBA8, SIZE, SIZE);
        glCreateFramebuffers(1, &m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_color, 0);

        glCreateTextures(GL_TEXTURE_2D, 1, &m_sceneDepth);
        glTextureStorage2D(m_sceneDepth, 1, GL_DEPTH_COMPONENT32F, SIZE, SIZE);
        glTextureParameteri(m_sceneDepth, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_sceneDepth, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const std::vector<float> depth(static_cast<size_t>(SIZE) * SIZE, 0.5f);
        glTextureSubImage2D(m_sceneDepth, 0, 0, 0, SIZE, SIZE,
                            GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, SIZE, SIZE);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void TearDown() override
    {
        if (m_fbo != 0)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &m_fbo);
            glDeleteTextures(1, &m_color);
            glDeleteTextures(1, &m_sceneDepth);
            glClipControl(static_cast<GLenum>(m_prevClipOrigin),
                          static_cast<GLenum>(m_prevClipDepth));
        }
    }

    static void queueRow(int row, float z)
    {
        const float y = rowToNdc(row);
        DebugDraw::line(glm::vec3(-1.0f, y, z), glm::vec3(1.0f, y, z), glm::vec3(1.0f));
    }

    /// Number of lit pixels in @a row of the colour target.
    static int litPixelsInRow(int row)
    {
        std::array<unsigned char, static_cast<size_t>(SIZE) * 4> pixels{};
        glReadPixels(0, row, SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        int lit = 0;
        for (int x = 0; x < SIZE; ++x)
        {
            if (pixels.at(static_cast<size_t>(x) * 4) > 128)
            {
                ++lit;
            }
        }
        return lit;
    }

    DebugDraw m_debugDraw;
    GLuint m_fbo = 0;
    GLuint m_color = 0;
    GLuint m_sceneDepth = 0;
    GLint m_prevClipOrigin = GL_LOWER_LEFT;
    GLint m_prevClipDepth = GL_NEGATIVE_ONE_TO_ONE;
};

}  // namespace

TEST_F(DebugDrawOcclusionTest, LineBehindSceneDepthIsHidden)
{
    queueRow(FRONT_ROW, 0.8f);
    queueRow(BACK_ROW, 0.2f);
    m_debugDraw.flush(glm::mat4(1.0f), m_sceneDepth);

    EXPECT_EQ(litPixelsInRow(FRONT_ROW), SIZE) << "a line in front of the scene must draw";
    EXPECT_EQ(litPixelsInRow(BACK_ROW), 0) << "a line behind the scene must be hidden";
}

TEST_F(DebugDrawOcclusionTest, LineRestingOnSurfaceStillDraws)
{
    // The ground grid sits a few centimetres above the ground; a line at
    // the scene's own depth must survive the depth-resolve margin.
    queueRow(FRONT_ROW, 0.5f);
    m_debugDraw.flush(glm::mat4(1.0f), m_sceneDepth);

    EXPECT_EQ(litPixelsInRow(FRONT_ROW), SIZE);
}

TEST_F(DebugDrawOcclusionTest, NoDepthTextureDrawsEveryLine)
{
    // The model-viewer preview flushes without scene depth.
    queueRow(FRONT_ROW, 0.8f);
    queueRow(BACK_ROW, 0.2f);
    m_debugDraw.flush(glm::mat4(1.0f));

    EXPECT_EQ(litPixelsInRow(FRONT_ROW), SIZE);
    EXPECT_EQ(litPixelsInRow(BACK_ROW), SIZE);
}

}  // namespace Vestige::Test
