// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_text_scale.cpp
/// @brief 3D_E-0994 — the interface scale grows the words inside menu
///        widgets, not only their boxes, and a text too wide for its box
///        is drawn smaller until it fits.

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include "renderer/text_renderer.h"
#include "systems/ui_system.h"
#include "ui/settings_page_layout.h"
#include "ui/sprite_batch_renderer.h"
#include "ui/ui_button.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_keybind_row.h"
#include "ui/ui_label.h"
#include "ui/ui_overlay_gl_state.h"
#include "ui/ui_panel.h"
#include "ui/ui_slider.h"
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

constexpr int W = 640;
constexpr int H = 160;
constexpr float kLeft = 10.0f;
constexpr float kTop = 10.0f;

/// The smallest rectangle holding every green pixel of one drawn frame, in
/// screen pixels (origin top-left). `height` is 0 when nothing green was drawn.
struct GreenExtent
{
    int left = W;
    int right = -1;
    int top = H;
    int bottom = -1;

    int height() const { return right < 0 ? 0 : bottom - top + 1; }
};

/// A theme whose primary text is the only green thing: strokes, the accent
/// and secondary text are blue or red.
Vestige::UITheme greenTextTheme(float scale)
{
    Vestige::UITheme theme;
    theme.textPrimary = glm::vec3(0.0f, 1.0f, 0.0f);
    theme.textSecondary = glm::vec3(0.0f, 0.0f, 1.0f);
    theme.accent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    theme.panelStroke = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    theme.panelStrokeStrong = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    return theme.withScale(scale);
}

class UITextScaleTest : public GLTestFixture
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
        ASSERT_TRUE(m_text.initialize(std::string(VESTIGE_FONT_DIR) + "/inter_tight.ttf",
                                      std::string(VESTIGE_FONT_DIR) + "/..", 48));
    }

    /// Draws @a element alone and returns where its green text landed.
    GreenExtent draw(std::unique_ptr<Vestige::UIElement> element)
    {
        Vestige::UICanvas canvas;
        canvas.addElement(std::move(element));

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
            Vestige::renderUICanvasLayers({&canvas}, m_batch, &m_text, W, H);
        }

        std::vector<unsigned char> px(static_cast<std::size_t>(W * H * 4));
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);

        GreenExtent extent;
        for (int glRow = 0; glRow < H; ++glRow)
        {
            for (int x = 0; x < W; ++x)
            {
                const std::size_t i = static_cast<std::size_t>((glRow * W + x) * 4);
                if (px[i + 1] > 127 && px[i] < 100 && px[i + 2] < 100)
                {
                    const int y = H - 1 - glRow;  // GL rows count from the bottom
                    extent.left = std::min(extent.left, x);
                    extent.right = std::max(extent.right, x);
                    extent.top = std::min(extent.top, y);
                    extent.bottom = std::max(extent.bottom, y);
                }
            }
        }
        return extent;
    }

    std::unique_ptr<Vestige::UIButton> button(const Vestige::UITheme& theme,
                                              const std::string& label, float width)
    {
        auto b = std::make_unique<Vestige::UIButton>();
        b->position = {kLeft, kTop};
        b->size = {width, theme.buttonHeight};
        b->label = label;
        b->theme = &theme;
        b->textRenderer = &m_text;
        return b;
    }

    std::unique_ptr<Vestige::UIDropdown> dropdown(const Vestige::UITheme& theme,
                                                  const std::string& label, float width)
    {
        auto d = std::make_unique<Vestige::UIDropdown>();
        d->position = {kLeft, kTop};
        d->size = {width, theme.dropdownHeight};
        d->options.push_back({"only", label});
        d->selectedIndex = 0;
        d->theme = &theme;
        d->textRenderer = &m_text;
        return d;
    }

    /// Capitals only, so the green extent's height is the cap height.
    const std::string kCaps = "HEMI";
    const std::string kLong = "HEMI HEMI HEMI HEMI HEMI HEMI HEMI HEMI HEMI HEMI";

    Vestige::SpriteBatchRenderer m_batch;
    Vestige::TextRenderer m_text;
};

/// The text is twice as tall at 2x as at 1x, to within a pixel of rounding
/// at each edge.
void expectDoubled(const GreenExtent& x1, const GreenExtent& x2, const char* what)
{
    ASSERT_GT(x1.height(), 0) << what << ": no text drawn at 1x";
    ASSERT_GT(x2.height(), 0) << what << ": no text drawn at 2x";
    EXPECT_NEAR(x2.height(), 2 * x1.height(), 2) << what;
}

} // namespace

TEST_F(UITextScaleTest, ButtonTextGrowsWithTheInterfaceScale)
{
    const Vestige::UITheme x1 = greenTextTheme(1.0f);
    const Vestige::UITheme x2 = greenTextTheme(2.0f);
    expectDoubled(draw(button(x1, kCaps, 600.0f)), draw(button(x2, kCaps, 600.0f)), "button");
}

TEST_F(UITextScaleTest, DropdownTextGrowsWithTheInterfaceScale)
{
    const Vestige::UITheme x1 = greenTextTheme(1.0f);
    const Vestige::UITheme x2 = greenTextTheme(2.0f);
    expectDoubled(draw(dropdown(x1, kCaps, 600.0f)), draw(dropdown(x2, kCaps, 600.0f)),
                  "dropdown");
}

TEST_F(UITextScaleTest, CheckboxLabelGrowsWithTheInterfaceScale)
{
    const auto checkbox = [this](const Vestige::UITheme& theme)
    {
        auto c = std::make_unique<Vestige::UICheckbox>();
        c->position = {kLeft, kTop};
        c->size = {24.0f, 20.0f};
        c->label = kCaps;
        c->theme = &theme;
        c->textRenderer = &m_text;
        return c;
    };
    const Vestige::UITheme x1 = greenTextTheme(1.0f);
    const Vestige::UITheme x2 = greenTextTheme(2.0f);
    expectDoubled(draw(checkbox(x1)), draw(checkbox(x2)), "checkbox label");
}

TEST_F(UITextScaleTest, KeybindRowTextGrowsWithTheInterfaceScale)
{
    const auto row = [this](const Vestige::UITheme& theme)
    {
        auto r = std::make_unique<Vestige::UIKeybindRow>();
        r->position = {kLeft, kTop};
        r->size = {600.0f, 2.0f * theme.keybindKeyHeight};
        r->label = kCaps;
        r->keyText = "H";
        r->theme = &theme;
        r->textRenderer = &m_text;
        return r;
    };
    const Vestige::UITheme x1 = greenTextTheme(1.0f);
    const Vestige::UITheme x2 = greenTextTheme(2.0f);
    // The row's label and its key are both primary text; the label is the
    // taller, so the extent's height is the label's.
    expectDoubled(draw(row(x1)), draw(row(x2)), "key-binding row");
}

TEST_F(UITextScaleTest, SliderValueGrowsWithTheInterfaceScale)
{
    const auto slider = [this](const Vestige::UITheme& theme)
    {
        auto s = std::make_unique<Vestige::UISlider>();
        s->position = {kLeft, kTop};
        s->size = {600.0f, theme.sliderHeight};
        s->formatter = [](float) { return std::string("HEMI"); };
        s->theme = &theme;
        s->textRenderer = &m_text;
        return s;
    };
    // The value readout is secondary text: make that the green one.
    Vestige::UITheme x1 = greenTextTheme(1.0f);
    Vestige::UITheme x2 = greenTextTheme(2.0f);
    for (Vestige::UITheme* theme : {&x1, &x2})
    {
        theme->textSecondary = glm::vec3(0.0f, 1.0f, 0.0f);
        theme->textPrimary = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    expectDoubled(draw(slider(x1)), draw(slider(x2)), "slider value");
}

TEST_F(UITextScaleTest, DropdownListRowsGrowWithTheInterfaceScale)
{
    // 60 px below the top of the open list is inside the second row at 1x
    // and, with rows twice as tall, inside the first at 2x.
    const float gapBelowBox = 4.0f;
    const float intoList = 60.0f;
    const auto rowUnder = [&](float scale)
    {
        const Vestige::UITheme theme = greenTextTheme(scale);
        auto d = dropdown(theme, kCaps, 600.0f);
        d->options.push_back({"second", kCaps});
        return d->optionAt({20.0f, d->size.y + gapBelowBox + intoList});
    };
    EXPECT_EQ(rowUnder(1.0f), 1);
    EXPECT_EQ(rowUnder(2.0f), 0);
}

TEST_F(UITextScaleTest, ButtonTextTooWideForItsBoxIsDrawnSmaller)
{
    const Vestige::UITheme theme = greenTextTheme(2.0f);
    const float width = 300.0f;
    const GreenExtent text = draw(button(theme, kLong, width));
    ASSERT_GT(text.height(), 0);
    EXPECT_LE(static_cast<float>(text.right), kLeft + width - theme.buttonPadX);
}

TEST_F(UITextScaleTest, DropdownTextTooWideForItsBoxIsDrawnSmaller)
{
    const Vestige::UITheme theme = greenTextTheme(2.0f);
    // The narrowest box the theme allows, and a text wider than it even at
    // the 1x text size.
    const float width = theme.dropdownMinWidth;
    const GreenExtent text = draw(dropdown(theme, kLong + " " + kLong, width));
    ASSERT_GT(text.height(), 0);
    EXPECT_LE(static_cast<float>(text.right), kLeft + width);
}

TEST_F(UITextScaleTest, SettingsRowLabelsGrowWithTheInterfaceScale)
{
    const char* const keys[] = {"HEMI"};
    const auto labelScale = [&keys](float scale)
    {
        const Vestige::UITheme theme = Vestige::UITheme::defaultTheme().withScale(scale);
        Vestige::UIPanel root;
        Vestige::SettingsPageLayout::addRowLabels(root, keys, 1, theme, nullptr);
        const auto* label = dynamic_cast<const Vestige::UILabel*>(root.getChildAt(0));
        return label != nullptr ? label->scale : 0.0f;
    };
    ASSERT_GT(labelScale(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(labelScale(2.0f), 2.0f * labelScale(1.0f));
}

TEST_F(UITextScaleTest, SettingsRowLabelTooWideForItsColumnIsDrawnSmaller)
{
    using namespace Vestige::SettingsPageLayout;
    const char* const keys[] = {"HEMI", kLong.c_str()};
    const Vestige::UITheme theme = Vestige::UITheme::defaultTheme().withScale(2.0f);
    Vestige::UIPanel root;
    addRowLabels(root, keys, 2, theme, &m_text);

    const auto* fits = dynamic_cast<const Vestige::UILabel*>(root.getChildAt(0));
    const auto* wide = dynamic_cast<const Vestige::UILabel*>(root.getChildAt(1));
    ASSERT_NE(fits, nullptr);
    ASSERT_NE(wide, nullptr);
    EXPECT_LT(wide->scale, fits->scale) << "only the label that does not fit shrinks";
    EXPECT_LE(wide->position.x + m_text.measureTextWidth(wide->text) * wide->scale, kControlX);
}
