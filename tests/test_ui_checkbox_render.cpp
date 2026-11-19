// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_checkbox_render.cpp
/// @brief 3D_E-0874 — what a checkbox draws: a focus ring when it holds
///        keyboard focus, and a check mark and click area that follow the
///        theme's box size.

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include "systems/ui_system.h"
#include "ui/sprite_batch_renderer.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_overlay_gl_state.h"
#include "ui/ui_theme.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using Vestige::Test::GLTestFixture;

namespace
{

constexpr int W = 128;
constexpr int H = 96;
constexpr float kBoxX = 20.0f;
constexpr float kBoxY = 20.0f;

/// One drawn frame, addressed in screen pixels (origin top-left).
struct Image
{
    std::vector<unsigned char> px;

    /// glReadPixels rows run bottom to top.
    static std::size_t index(int x, int y)
    {
        const std::size_t row = static_cast<std::size_t>(H - 1 - y);
        return ((row * static_cast<std::size_t>(W)) + static_cast<std::size_t>(x)) * 4u;
    }

    bool isRed(int x, int y) const
    {
        const std::size_t i = index(x, y);
        return px[i] > 127 && px[i + 1] < 100 && px[i + 2] < 100;
    }

    bool isGreen(int x, int y) const
    {
        const std::size_t i = index(x, y);
        return px[i + 1] > 127 && px[i] < 100 && px[i + 2] < 100;
    }
};

/// A theme whose three checkbox colours sit in separate channels: the fill
/// and the focus ring are red, the check mark is green, the stroke is blue.
Vestige::UITheme channelTheme(float scale)
{
    Vestige::UITheme theme;
    theme.accent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    theme.accentInk = glm::vec3(0.0f, 1.0f, 0.0f);
    theme.panelStrokeStrong = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    theme.textPrimary = glm::vec3(0.0f, 0.0f, 1.0f);
    return theme.withScale(scale);
}

class UICheckboxRenderTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        ASSERT_TRUE(m_batch.initialize(std::string(VESTIGE_FONT_DIR) + "/.."));
    }

    /// Adds a label-less checkbox at (kBoxX, kBoxY), sized as the settings
    /// pages size theirs.
    Vestige::UICheckbox* addCheckbox(const Vestige::UITheme& theme)
    {
        auto box = std::make_unique<Vestige::UICheckbox>();
        box->position = {kBoxX, kBoxY};
        box->size = {24.0f, 20.0f};
        box->theme = &theme;
        Vestige::UICheckbox* raw = box.get();
        m_canvas.addElement(std::move(box));
        return raw;
    }

    Image draw()
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
            Vestige::renderUICanvasLayers({&m_canvas}, m_batch, nullptr, W, H);
        }

        Image image;
        image.px.resize(static_cast<std::size_t>(W * H * 4));
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, image.px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        return image;
    }

    Vestige::SpriteBatchRenderer m_batch;
    Vestige::UICanvas m_canvas;
};

/// Red pixels outside the box square: only a focus ring puts any there.
int redPixelsOutsideBox(const Image& image, float boxSize)
{
    int count = 0;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const bool inside = static_cast<float>(x) >= kBoxX
                             && static_cast<float>(x) < kBoxX + boxSize
                             && static_cast<float>(y) >= kBoxY
                             && static_cast<float>(y) < kBoxY + boxSize;
            count += (!inside && image.isRed(x, y)) ? 1 : 0;
        }
    }
    return count;
}

/// Width and height of the smallest rectangle holding every green pixel.
glm::ivec2 checkMarkExtent(const Image& image)
{
    int minX = W;
    int maxX = -1;
    int minY = H;
    int maxY = -1;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            if (image.isGreen(x, y))
            {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    return (maxX < 0) ? glm::ivec2(0) : glm::ivec2(maxX - minX + 1, maxY - minY + 1);
}

} // namespace

TEST_F(UICheckboxRenderTest, KeyboardFocusDrawsARingAroundTheBox)
{
    const Vestige::UITheme theme = channelTheme(1.0f);
    Vestige::UICheckbox* box = addCheckbox(theme);

    EXPECT_EQ(redPixelsOutsideBox(draw(), theme.checkboxSize), 0)
        << "an unfocused checkbox drew accent colour outside its box";

    box->focused = true;
    EXPECT_GT(redPixelsOutsideBox(draw(), theme.checkboxSize), 0)
        << "a focused checkbox shows no focus ring";
}

TEST_F(UICheckboxRenderTest, CheckMarkGrowsWithTheBox)
{
    const Vestige::UITheme normal = channelTheme(1.0f);
    const Vestige::UITheme doubled = channelTheme(2.0f);
    Vestige::UICheckbox* box = addCheckbox(normal);
    box->checked = true;

    const glm::ivec2 atNormal = checkMarkExtent(draw());
    ASSERT_GT(atNormal.x, 0) << "the check mark drew nothing";

    box->theme = &doubled;
    const glm::ivec2 atDoubled = checkMarkExtent(draw());

    // Twice the box, so twice the mark; two pixels of slack for rounding.
    EXPECT_GE(atDoubled.x, 2 * atNormal.x - 2);
    EXPECT_GE(atDoubled.y, 2 * atNormal.y - 2);
}

TEST_F(UICheckboxRenderTest, ClickAreaCoversTheDrawnBox)
{
    const Vestige::UITheme doubled = channelTheme(2.0f);
    Vestige::UICheckbox* box = addCheckbox(doubled);
    draw();

    // Near the bottom-right corner of the 40 px box the theme now draws.
    const glm::vec2 corner(kBoxX + doubled.checkboxSize - 4.0f,
                           kBoxY + doubled.checkboxSize - 4.0f);
    glm::vec2 absPos(0.0f);
    EXPECT_EQ(m_canvas.findInteractiveAt(corner, W, H, absPos), box)
        << "a click inside the drawn box missed the checkbox";
}
