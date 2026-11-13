// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ibl_prefilter.cpp
/// @brief Shared cube mesh, views and face pass for IBL captures.
#include "renderer/ibl_prefilter.h"
#include "renderer/framebuffer.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>

namespace Vestige
{

namespace
{

// Inward-facing unit cube around the origin: 12 triangles, position only.
constexpr float CUBE_VERTICES[] = {
    -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,
};

constexpr GLsizei CUBE_VERTEX_COUNT = 36;

} // namespace

const glm::mat4* iblCaptureViews()
{
    static const glm::mat4 views[6] = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    };
    return views;
}

glm::mat4 iblCaptureProjection()
{
    // 90 degrees so each face sees exactly its sixth of the sphere. The cube's
    // corners are sqrt(3) from the origin; any far plane past that captures
    // the same image, and the nearer one keeps the depth range tight.
    return glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
}

void createIblCubeMesh(GLuint& vao, GLuint& vbo)
{
    glCreateBuffers(1, &vbo);
    glNamedBufferStorage(vbo, sizeof(CUBE_VERTICES), CUBE_VERTICES, 0);

    glCreateVertexArrays(1, &vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, 3 * sizeof(float));
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
}

void drawIblCube(GLuint vao)
{
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, CUBE_VERTEX_COUNT);
    glBindVertexArray(0);
}

bool runIblFacePass(const IblFacePassParams& params)
{
    glNamedRenderbufferStorage(params.captureRbo, GL_DEPTH_COMPONENT24,
                               params.resolution, params.resolution);
    glNamedFramebufferRenderbuffer(params.captureFbo, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, params.captureRbo);
    // Attach face 0 so completeness is checked once before the loop swaps layers.
    glNamedFramebufferTextureLayer(params.captureFbo, GL_COLOR_ATTACHMENT0,
                                   params.target, 0, 0);
    const GLenum status = glCheckNamedFramebufferStatus(params.captureFbo, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("IBL capture framebuffer incomplete — status: "
            + framebufferStatusText(status));
        return false;
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, params.captureFbo);
    glViewport(0, 0, params.resolution, params.resolution);
    glDisable(GL_CULL_FACE);

    const glm::mat4* views = iblCaptureViews();
    for (int face = 0; face < 6; face++)
    {
        params.shader->setMat4("u_view", views[face]);
        glNamedFramebufferTextureLayer(params.captureFbo, GL_COLOR_ATTACHMENT0,
                                       params.target, 0, face);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (params.drawCube) params.drawCube();
    }

    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return true;
}

} // namespace Vestige
