// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_canvas_layers.cpp
/// @brief 3D_E-0749 — a modal canvas covers the text of the canvas under
///        it, not only its panels.

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include "renderer/text_renderer.h"
#include "systems/ui_system.h"
#include "ui/sprite_batch_renderer.h"
#include "ui/ui_canvas.h"
#include "ui/ui_label.h"
#include "ui/ui_overlay_gl_state.h"
#include "ui/ui_panel.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using Vestige::Test::GLTestFixture;

namespace
{

class UICanvasLayersTest : public GLTestFixture {};

constexpr int W = 128;
constexpr int H = 64;

/// Draws @a layers into a fresh W x H image and returns how many pixels
/// came out bright (red channel above half).
int brightPixels(std::initializer_list<Vestige::UICanvas*> layers,
                 Vestige::SpriteBatchRenderer& batch, Vestige::TextRenderer& text)
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
    {
        Vestige::Test::ScopedLeakCheckDisable noLeakTracking;  // llvmpipe JIT
        const Vestige::UIOverlayGlState overlay;
        Vestige::renderUICanvasLayers(layers, batch, &text, W, H);
    }

    std::vector<unsigned char> px(static_cast<std::size_t>(W * H * 4));
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);

    int bright = 0;
    for (std::size_t i = 0; i < px.size(); i += 4)
    {
        bright += (px[i] > 127) ? 1 : 0;
    }
    return bright;
}

} // namespace

TEST_F(UICanvasLayersTest, ModalCoversTheTextOfTheCanvasUnderIt)
{
    Vestige::TextRenderer text;
    ASSERT_TRUE(text.initialize(std::string(VESTIGE_FONT_DIR) + "/inter_tight.ttf",
                                std::string(VESTIGE_FONT_DIR) + "/..", 48));
    Vestige::SpriteBatchRenderer batch;
    ASSERT_TRUE(batch.initialize(std::string(VESTIGE_FONT_DIR) + "/.."));

    Vestige::UICanvas root;
    auto label = std::make_unique<Vestige::UILabel>();
    label->text = "HHHH";
    label->position = {8.0f, 8.0f};
    label->scale = 0.8f;
    label->color = glm::vec3(1.0f);
    label->textRenderer = &text;
    root.addElement(std::move(label));

    Vestige::UICanvas modal;
    auto panel = std::make_unique<Vestige::UIPanel>();
    panel->size = {static_cast<float>(W), static_cast<float>(H)};
    panel->backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    modal.addElement(std::move(panel));

    ASSERT_GT(brightPixels({&root}, batch, text), 50) << "the label drew nothing";
    EXPECT_EQ(brightPixels({&root, &modal}, batch, text), 0)
        << "the root's text shows through the modal's opaque panel";
}
