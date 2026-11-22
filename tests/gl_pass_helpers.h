// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#pragma once

/// @file gl_pass_helpers.h
/// @brief Run one real full-screen shader pass in a test and read back every
///        pixel (3D_E-0874's full-pass shader rig).
///
/// Use inside a `GLTestFixture` test. Load the engine's own shader files with
/// `Shader::loadFromFiles(passShaderPath("screen_quad.vert.glsl"), ...)`, set
/// its uniforms, bind inputs made by `FloatTexture`, then call
/// `drawFullScreenRed` to draw into a single-channel float image of the size
/// given. Inputs are sampled with nearest filtering, so a test controls
/// exactly which texel each sample reads.

#include "lsan_guard.h"

#include <glad/gl.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Vestige::Test
{

/// @brief Path of a file in the engine's shader folder.
inline std::string passShaderPath(const std::string& name)
{
    return std::string(VESTIGE_SHADER_DIR) + "/" + name;
}

/// @brief A single-channel float texture (GL_R32F), nearest-filtered and
///        clamped, filled row by row from the bottom row up.
struct FloatTexture
{
    GLuint id = 0;
    FloatTexture(int width, int height, const std::vector<float>& texels)
    {
        glCreateTextures(GL_TEXTURE_2D, 1, &id);
        glTextureStorage2D(id, 1, GL_R32F, width, height);
        glTextureSubImage2D(id, 0, 0, 0, width, height, GL_RED, GL_FLOAT, texels.data());
        glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    ~FloatTexture() { glDeleteTextures(1, &id); }
    FloatTexture(const FloatTexture&) = delete;
    FloatTexture& operator=(const FloatTexture&) = delete;
    void bind(GLuint unit) const { glBindTextureUnit(unit, id); }
};

/// @brief Draws the bound program over a `width` x `height` GL_R32F image
///        with a full-screen triangle (location 0 = position, location 1 =
///        texcoord, as screen_quad.vert.glsl reads) and returns its pixels,
///        bottom row first. Returns an empty vector if the image could not
///        be made.
inline std::vector<float> drawFullScreenRed(int width, int height)
{
    const float v[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         3.0f, -1.0f, 2.0f, 0.0f,
        -1.0f,  3.0f, 0.0f, 2.0f,
    };
    GLuint vao = 0;
    GLuint vbo = 0;
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);
    glNamedBufferStorage(vbo, sizeof(v), v, 0);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, 4 * sizeof(float));
    glEnableVertexArrayAttrib(vao, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
    glVertexArrayAttribBinding(vao, 0, 0);
    glVertexArrayAttribBinding(vao, 1, 0);

    GLuint target = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &target);
    glTextureStorage2D(target, 1, GL_R32F, width, height);
    GLuint fbo = 0;
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, target, 0);

    std::vector<float> out;
    if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindVertexArray(vao);
        {
            ScopedLeakCheckDisable noLeakTracking;  // llvmpipe JIT
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindVertexArray(0);
        out.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        glReadPixels(0, 0, width, height, GL_RED, GL_FLOAT, out.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &target);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    return out;
}

}  // namespace Vestige::Test
