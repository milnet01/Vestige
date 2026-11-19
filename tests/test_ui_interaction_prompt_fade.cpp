// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_interaction_prompt_fade.cpp
/// @brief 3D_E-0874 — a fading interaction prompt becomes see-through: it
///        blends toward what is behind it, not toward black.

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include "renderer/camera.h"
#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"
#include "ui/ui_interaction_prompt.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using Vestige::Test::GLTestFixture;

namespace
{

constexpr int W = 256;
constexpr int H = 128;

struct RedRange
{
    int darkest = 255;
    int brightest = 0;
};

class UIInteractionPromptFadeTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        ASSERT_TRUE(m_text.initialize(std::string(VESTIGE_FONT_DIR) + "/inter_tight.ttf",
                                      std::string(VESTIGE_FONT_DIR) + "/..", 48));
    }

    /// Draws a white prompt @a distance metres in front of the camera over
    /// a background of @a backgroundGrey, and returns the darkest and
    /// brightest red value in the image. The prompt is fully opaque up to
    /// 2.5 m and gone at 4 m (its defaults).
    RedRange drawPrompt(float distance, float backgroundGrey)
    {
        Vestige::Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));  // looks down -Z
        Vestige::UIInteractionPrompt prompt;
        prompt.worldPosition = glm::vec3(0.0f, 0.0f, 3.0f - distance);
        prompt.screenOffset = glm::vec2(-100.0f, 0.0f);
        prompt.camera = &camera;
        prompt.textRenderer = &m_text;

        GLuint tex = 0;
        GLuint fbo = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glViewport(0, 0, W, H);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(backgroundGrey, backgroundGrey, backgroundGrey, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        {
            Vestige::Test::ScopedLeakCheckDisable noLeakTracking;  // llvmpipe JIT
            Vestige::SpriteBatchRenderer unusedBatch;  // the prompt draws text only
            prompt.render(unusedBatch, glm::vec2(0.0f), W, H);
        }

        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4u);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);

        RedRange range;
        for (std::size_t i = 0; i < px.size(); i += 4)
        {
            range.darkest = std::min(range.darkest, static_cast<int>(px[i]));
            range.brightest = std::max(range.brightest, static_cast<int>(px[i]));
        }
        return range;
    }

    Vestige::TextRenderer m_text;
};

constexpr float kNear = 2.0f;       // closer than fadeNear: fully opaque
constexpr float kHalfFaded = 3.25f; // midway between fadeNear and fadeFar

} // namespace

TEST_F(UIInteractionPromptFadeTest, HalfFadedWhitePromptLeavesAWhiteBackgroundWhite)
{
    ASSERT_GT(drawPrompt(kNear, 0.0f).brightest, 200) << "the prompt drew nothing";

    // White text at any transparency over white is still white. Text faded
    // by darkening its colour shows as grey lettering instead.
    EXPECT_GE(drawPrompt(kHalfFaded, 1.0f).darkest, 250)
        << "a fading white prompt darkened the white behind it";
}

TEST_F(UIInteractionPromptFadeTest, HalfFadedPromptIsDimmerOverBlackThanAnOpaqueOne)
{
    const int opaque = drawPrompt(kNear, 0.0f).brightest;
    const int faded = drawPrompt(kHalfFaded, 0.0f).brightest;
    ASSERT_GT(opaque, 200) << "the prompt drew nothing";

    EXPECT_GT(faded, 60) << "a half-faded prompt vanished";
    EXPECT_LT(faded, 200) << "a half-faded prompt drew at full strength";
}
