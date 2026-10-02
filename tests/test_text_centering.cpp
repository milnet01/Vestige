// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_text_centering.cpp
/// @brief 3D_E-0748 — TextRenderer::topForCenteredCaps puts a line of
///        capitals on the requested centre line. Every widget that centres
///        text in a box (button, dropdown, checkbox, slider, key-binding row)
///        places its text through it.

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include "renderer/text_renderer.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <string>
#include <vector>

using Vestige::Test::GLTestFixture;

namespace
{

class TextCenteringTest : public GLTestFixture {};

/// Renders @a text at the y the helper returns for @a centerY into a W x H
/// image, and returns the mean row of the lit pixels (top-left origin), or
/// -1 when nothing was drawn.
float litRowCentre(Vestige::TextRenderer& tr, const std::string& text,
                   float centerY, float scale, int W, int H)
{
    GLuint tex = 0;
    GLuint fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    {
        Vestige::Test::ScopedLeakCheckDisable noLeakTracking;  // llvmpipe JIT
        tr.renderText2D(text, 4.0f, tr.topForCenteredCaps(centerY, scale), scale,
                        glm::vec3(1.0f), W, H);
    }

    std::vector<unsigned char> px(static_cast<std::size_t>(W * H * 4));
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDisable(GL_BLEND);

    double rowSum = 0.0;
    double weight = 0.0;
    for (int glRow = 0; glRow < H; ++glRow)
    {
        for (int x = 0; x < W; ++x)
        {
            const unsigned char r = px[static_cast<std::size_t>((glRow * W + x) * 4)];
            if (r > 127)
            {
                const int screenRow = H - 1 - glRow;  // GL rows count from the bottom
                rowSum += screenRow + 0.5;
                weight += 1.0;
            }
        }
    }
    return weight > 0.0 ? static_cast<float>(rowSum / weight) : -1.0f;
}

} // namespace

TEST_F(TextCenteringTest, CapitalsCentreOnTheRequestedLine)
{
    Vestige::TextRenderer tr;
    ASSERT_TRUE(tr.initialize(std::string(VESTIGE_FONT_DIR) + "/inter_tight.ttf",
                              std::string(VESTIGE_FONT_DIR) + "/..", 48));

    // The button and checkbox scales, at two box centres.
    for (const float scale : {0.4f, 0.26f})
    {
        for (const float centre : {40.0f, 70.0f})
        {
            const float lit = litRowCentre(tr, "HEMI", centre, scale, 128, 128);
            ASSERT_GE(lit, 0.0f) << "nothing drawn at scale " << scale;
            EXPECT_NEAR(lit, centre, 1.5f) << "scale " << scale;
        }
    }
}
