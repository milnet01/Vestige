// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_overlay_gl_state.cpp
/// @brief 3D_E-0747 — the UI overlay draws even when the 3D pass left
///        back-face culling on, and the culling switch comes back after.

#include "gl_test_fixture.h"

#include "ui/sprite_batch_renderer.h"
#include "ui/ui_overlay_gl_state.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <string>

using Vestige::Test::GLTestFixture;

namespace
{

class UIOverlayGlStateTest : public GLTestFixture {};

} // namespace

TEST_F(UIOverlayGlStateTest, SpriteDrawsWithCullingLeftOnAndCullingIsRestored)
{
    constexpr int W = 32;
    constexpr int H = 32;

    Vestige::SpriteBatchRenderer batch;
    ASSERT_TRUE(batch.initialize(std::string(VESTIGE_FONT_DIR) + "/.."));

    GLuint tex = 0;
    GLuint fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_CULL_FACE);  // as the 3D pass leaves it
    {
        const Vestige::UIOverlayGlState overlay;
        batch.begin(W, H);
        batch.drawQuad({0.0f, 0.0f}, {static_cast<float>(W), static_cast<float>(H)},
                       glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        batch.end();
    }
    EXPECT_EQ(glIsEnabled(GL_CULL_FACE), GL_TRUE);

    std::array<unsigned char, 4> px{};
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    EXPECT_GT(px[0], 250) << "the quad was culled: the overlay drew nothing";
    EXPECT_LT(px[1], 5);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDisable(GL_CULL_FACE);
}
