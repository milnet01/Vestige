// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_point_shadow_skinning.cpp
/// @brief 3D_E-0874 — the point-light shadow shader poses skinned meshes.
///
/// The directional shadow pass skins each mesh with its bone matrices; the
/// point-light pass did not, so an animated character cast its point-light
/// shadow in the bind pose. The test draws one triangle with
/// point_shadow_depth whose vertices sit outside the view, and a bone that
/// moves it into the view. Only a skinning shader draws it.
#include "gl_test_fixture.h"
#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>

namespace Vestige::Test
{

namespace
{

class PointShadowSkinningTest : public GLTestFixture {};

struct SkinnedVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::ivec4 boneIds;
    glm::vec4 boneWeights;
};

} // namespace

TEST_F(PointShadowSkinningTest, BoneMovesTheTriangleIntoTheShadowMap)
{
    const std::string dir = std::string(VESTIGE_SHADER_DIR) + "/";
    Shader shader;
    ASSERT_TRUE(shader.loadFromFiles(dir + "point_shadow_depth.vert.glsl",
                                     dir + "point_shadow_depth.frag.glsl"));

    // A triangle covering the view's centre, but shifted 10 units along X,
    // outside clip space. Bone 0 shifts it back.
    const glm::vec3 offset(10.0f, 0.0f, 0.0f);
    const std::array<SkinnedVertex, 3> vertices = {{
        {glm::vec3(-0.9f, -0.9f, 0.0f) + offset, glm::vec3(0, 0, 1), glm::ivec4(0), glm::vec4(1, 0, 0, 0)},
        {glm::vec3(0.9f, -0.9f, 0.0f) + offset, glm::vec3(0, 0, 1), glm::ivec4(0), glm::vec4(1, 0, 0, 0)},
        {glm::vec3(0.0f, 0.9f, 0.0f) + offset, glm::vec3(0, 0, 1), glm::ivec4(0), glm::vec4(1, 0, 0, 0)},
    }};
    const glm::mat4 bone = glm::translate(glm::mat4(1.0f), -offset);

    GLuint vbo = 0;
    glCreateBuffers(1, &vbo);
    glNamedBufferStorage(vbo, sizeof(vertices), vertices.data(), 0);
    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(SkinnedVertex));
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(SkinnedVertex, position));
    glVertexArrayAttribBinding(vao, 0, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(SkinnedVertex, normal));
    glVertexArrayAttribBinding(vao, 1, 0);
    // Locations 10 and 11: the mesh layout's bone ids and weights.
    glEnableVertexArrayAttrib(vao, 10);
    glVertexArrayAttribIFormat(vao, 10, 4, GL_INT, offsetof(SkinnedVertex, boneIds));
    glVertexArrayAttribBinding(vao, 10, 0);
    glEnableVertexArrayAttrib(vao, 11);
    glVertexArrayAttribFormat(vao, 11, 4, GL_FLOAT, GL_FALSE, offsetof(SkinnedVertex, boneWeights));
    glVertexArrayAttribBinding(vao, 11, 0);

    GLuint bones = 0;
    glCreateBuffers(1, &bones);
    glNamedBufferStorage(bones, sizeof(glm::mat4), &bone, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bones);

    // fluxOut lands in an RGBA8 target; alpha 1 marks a drawn pixel.
    constexpr int kSize = 8;
    GLuint colour = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &colour);
    glTextureStorage2D(colour, 1, GL_RGBA8, kSize, kSize);
    GLuint fbo = 0;
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, colour, 0);
    ASSERT_EQ(glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER),
              static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, kSize, kSize);
    const std::array<float, 4> clear = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearNamedFramebufferfv(fbo, GL_COLOR, 0, clear.data());
    const GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullWasOn = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    shader.use();
    shader.setMat4("u_model", glm::mat4(1.0f));
    shader.setMat4("u_lightSpaceMatrix", glm::mat4(1.0f));
    shader.setBool("u_useInstancing", false);
    shader.setBool("u_hasBones", true);
    shader.setVec3("u_lightPos", glm::vec3(0.0f, 0.0f, 1.0f));
    shader.setFloat("u_farPlane", 25.0f);
    shader.setBool("u_hasTexture", false);
    shader.setVec3("u_albedoFactor", glm::vec3(1.0f));
    shader.setVec3("u_lightRadiance", glm::vec3(1.0f));
    shader.setFloat("u_attConstant", 1.0f);
    shader.setFloat("u_attLinear", 0.0f);
    shader.setFloat("u_attQuadratic", 0.0f);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    std::array<unsigned char, 4> centre = {0, 0, 0, 0};
    glReadPixels(kSize / 2, kSize / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centre.data());
    EXPECT_EQ(centre[3], 255) << "the triangle stayed in its bind pose, outside the shadow map";
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (depthWasOn == GL_TRUE)
    {
        glEnable(GL_DEPTH_TEST);
    }
    if (cullWasOn == GL_TRUE)
    {
        glEnable(GL_CULL_FACE);
    }
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &colour);
    glDeleteBuffers(1, &bones);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
}

} // namespace Vestige::Test
