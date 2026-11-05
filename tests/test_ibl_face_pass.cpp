// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ibl_face_pass.cpp
/// @brief 3D_E-0870 — the shared cube-face pass behind every IBL capture.
///
/// EnvironmentMap's capture and irradiance passes and LightProbe's irradiance
/// pass all draw the capture cube once per cubemap face through
/// runIblFacePass. A test shader writes each pixel's view direction, so a
/// face drawn with the wrong view, a face skipped, or a viewport smaller than
/// the target all show up in the read-back. An incomplete target must
/// report failure.
#include "gl_test_fixture.h"
#include "renderer/ibl_prefilter.h"
#include "renderer/shader.h"
#include "test_helpers.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

constexpr int kRes = 8;

class IblFacePassTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        // GTEST_SKIP returns from the base SetUp only; with no GL context
        // every gl* pointer is null and the calls below would crash.
        if (IsSkipped())
        {
            return;
        }
        m_fragPath = (std::filesystem::temp_directory_path()
            / ("vestige_ibl_face_pass_" + Testing::vestigeTestStamp() + ".frag.glsl")).string();
        {
            std::ofstream frag(m_fragPath);
            frag << "#version 450 core\n"
                    "in vec3 v_texCoord;\n"
                    "out vec4 fragColor;\n"
                    "void main() { fragColor = vec4(normalize(v_texCoord), 1.0); }\n";
        }
        ASSERT_TRUE(m_shader.loadFromFiles(
            std::string(VESTIGE_SHADER_DIR) + "/cubemap_render.vert.glsl", m_fragPath));
        createIblCubeMesh(m_cubeVao, m_cubeVbo);
        glCreateFramebuffers(1, &m_fbo);
        glCreateRenderbuffers(1, &m_rbo);
    }

    void TearDown() override
    {
        if (IsSkipped())
        {
            return;
        }
        glDeleteRenderbuffers(1, &m_rbo);
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteBuffers(1, &m_cubeVbo);
        glDeleteVertexArrays(1, &m_cubeVao);
        std::filesystem::remove(m_fragPath);
        GLTestFixture::TearDown();
    }

    GLuint makeCubemap(GLenum format) const
    {
        GLuint tex = 0;
        glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &tex);
        glTextureStorage2D(tex, 1, format, kRes, kRes);
        return tex;
    }

    IblFacePassParams params(GLuint target)
    {
        m_shader.use();
        m_shader.setMat4("u_projection", iblCaptureProjection());
        IblFacePassParams p;
        p.shader = &m_shader;
        p.captureFbo = m_fbo;
        p.captureRbo = m_rbo;
        p.target = target;
        p.resolution = kRes;
        p.drawCube = [this]() { drawIblCube(m_cubeVao); };
        return p;
    }

    std::string m_fragPath;
    Shader m_shader;
    GLuint m_cubeVao = 0;
    GLuint m_cubeVbo = 0;
    GLuint m_fbo = 0;
    GLuint m_rbo = 0;
};

/// One face's texels as RGBA floats.
std::vector<float> readFace(GLuint cubemap, int face)
{
    std::vector<float> texels(static_cast<size_t>(kRes * kRes * 4), 0.0f);
    glGetTextureSubImage(cubemap, 0, 0, 0, face, kRes, kRes, 1, GL_RGBA, GL_FLOAT,
                         static_cast<GLsizei>(texels.size() * sizeof(float)), texels.data());
    return texels;
}

} // namespace

TEST_F(IblFacePassTest, EveryFaceIsCoveredAndLooksAlongItsAxis)
{
    // GL cubemap face order: +X, -X, +Y, -Y, +Z, -Z.
    const std::array<glm::vec3, 6> axes = {{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    }};
    const GLuint target = makeCubemap(GL_RGBA16F);

    ASSERT_TRUE(runIblFacePass(params(target)));

    for (int face = 0; face < 6; ++face)
    {
        const auto texels = readFace(target, face);
        glm::vec3 sum(0.0f);
        for (size_t i = 0; i < texels.size(); i += 4)
        {
            ASSERT_EQ(texels[i + 3], 1.0f) << "face " << face << " texel " << i / 4 << " not drawn";
            sum += glm::vec3(texels[i], texels[i + 1], texels[i + 2]);
        }
        // The average view direction over a face points along its axis.
        EXPECT_GT(glm::dot(glm::normalize(sum), axes[static_cast<size_t>(face)]), 0.99f)
            << "face " << face;
    }
    glDeleteTextures(1, &target);
}

TEST_F(IblFacePassTest, IncompleteTargetReportsFailure)
{
    // A depth format cannot be a colour attachment: the framebuffer is incomplete.
    const GLuint target = makeCubemap(GL_DEPTH_COMPONENT24);

    EXPECT_FALSE(runIblFacePass(params(target)));
    while (glGetError() != GL_NO_ERROR) {}  // the failed attach may raise an error; not under test
    glDeleteTextures(1, &target);
}
