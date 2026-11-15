// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sprite_renderer_gl_state.cpp
/// @brief SpriteRenderer::end() puts back the GL blend state begin() found.
///
/// The sprite pass runs between the 3D post-process and the UI overlay, and
/// a transparent batch sets its own blend function. Whatever the pass before
/// it left, colour and alpha halves alike, must be there again after end().
#include "gl_test_fixture.h"
#include "renderer/sprite_renderer.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

class SpriteRendererGlState : public GLTestFixture
{
protected:
    void TearDown() override
    {
        // The GL context is shared by every test: leave GL's defaults.
        glDisable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ZERO);
    }
};

struct BlendFunc
{
    GLint srcRgb = 0;
    GLint dstRgb = 0;
    GLint srcAlpha = 0;
    GLint dstAlpha = 0;
};

BlendFunc readBlendFunc()
{
    BlendFunc f;
    glGetIntegerv(GL_BLEND_SRC_RGB, &f.srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &f.dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &f.srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &f.dstAlpha);
    return f;
}

void expectBlendFunc(const BlendFunc& f, GLint srcRgb, GLint dstRgb, GLint srcAlpha,
                     GLint dstAlpha)
{
    EXPECT_EQ(f.srcRgb, srcRgb) << "colour source factor";
    EXPECT_EQ(f.dstRgb, dstRgb) << "colour destination factor";
    EXPECT_EQ(f.srcAlpha, srcAlpha) << "alpha source factor";
    EXPECT_EQ(f.dstAlpha, dstAlpha) << "alpha destination factor";
}

} // namespace

// 3D_E-0874 lane-05: end() restored only the alpha half, for both halves, and
// left the function changed when blending had been off.
TEST_F(SpriteRendererGlState, EndRestoresTheWholeBlendFunction)
{
    SpriteRenderer renderer;
    ASSERT_TRUE(renderer.initialize(std::string(VESTIGE_SHADER_DIR) + "/.."));

    for (const bool blendOn : {true, false})
    {
        SCOPED_TRACE(blendOn ? "blend was on" : "blend was off");
        if (blendOn)
        {
            glEnable(GL_BLEND);
        }
        else
        {
            glDisable(GL_BLEND);
        }
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

        renderer.begin(glm::mat4(1.0f));
        // What a transparent batch sets (SpriteRenderer::drawBatch).
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        renderer.end();

        EXPECT_EQ(glIsEnabled(GL_BLEND), blendOn ? GL_TRUE : GL_FALSE);
        expectBlendFunc(readBlendFunc(), GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    }
    renderer.shutdown();
}
