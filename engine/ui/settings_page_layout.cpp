// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/settings_page_layout.h"

#include "localization/localization_service.h"
#include "renderer/text_renderer.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_label.h"
#include "ui/ui_scroll_rows.h"
#include "ui/ui_slider.h"
#include "ui/ui_theme.h"

#include <cmath>
#include <utility>

namespace Vestige
{

namespace SettingsPageLayout
{

namespace
{

// At the default theme a row is 52 px for a 40 px dropdown: the control and
// a gap of three tenths of its height.
constexpr float kRowStepPerControlHeight = 1.3f;
constexpr float kDefaultControlHeight = 40.0f;

// A length drawn for the default theme, grown with the interface scale.
float scaled(float length, const UITheme& theme)
{
    return length * theme.dropdownHeight / kDefaultControlHeight;
}

} // namespace

float rowStep(const UITheme& theme)
{
    return theme.dropdownHeight * kRowStepPerControlHeight;
}

float rowY(int row, const UITheme& theme)
{
    return kFirstRow + rowStep(theme) * static_cast<float>(row);
}

std::unique_ptr<UIScrollRows> makePageRoot(const UITheme& theme, std::size_t rowCount)
{
    auto root = std::make_unique<UIScrollRows>();
    root->position = {0.0f, 0.0f};
    root->size = {1920.0f, 1080.0f};
    root->anchor = Anchor::TOP_LEFT;
    root->interactive = false;
    root->viewTop = kFirstRow;
    root->viewHeight = kRowsBottom - kFirstRow;
    root->rowStep = rowStep(theme);
    root->rowCount = static_cast<int>(rowCount);
    root->scrollBarX = kScrollBarX;
    root->theme = &theme;
    // A grouping element, not a panel a screen reader should announce.
    root->accessible().role = UIAccessibleRole::Unknown;
    return root;
}

void addRowLabels(UIElement& root, const char* const* keys, std::size_t count,
                  const UITheme& theme, TextRenderer* text)
{
    for (std::size_t row = 0; row < count; ++row)
    {
        auto label = std::make_unique<UILabel>();
        label->text = std::string(tr(keys[row]));
        label->position = {kLabelX, rowY(static_cast<int>(row), theme) + scaled(10.0f, theme)};
        // Text grows with the interface scale; a label too wide for its
        // column is drawn smaller, so it never runs under the control
        // (3D_E-0994, user 2026-11-20).
        label->scale = 0.34f * theme.defaultTextScale;
        if (text != nullptr)
        {
            label->scale = text->scaleToFit(label->text, label->scale,
                                            kControlX - kLabelX - scaled(16.0f, theme));
        }
        label->color = theme.textPrimary;
        label->textRenderer = text;
        root.addChild(std::move(label));
    }
}

std::unique_ptr<UIDropdown> makeDropdown(const char* const* keys, std::size_t count,
                                         const UITheme& theme, TextRenderer* text,
                                         int row, const char* labelKey)
{
    auto d = std::make_unique<UIDropdown>();
    for (std::size_t i = 0; i < count; ++i)
    {
        d->options.push_back({keys[i], std::string(tr(keys[i]))});
    }
    d->position = {kControlX, rowY(row, theme)};
    d->size = {320.0f, 40.0f};
    d->theme = &theme;
    d->textRenderer = text;
    d->accessible().label = std::string(tr(labelKey));
    return d;
}

std::unique_ptr<UICheckbox> makeCheckbox(const UITheme& theme, TextRenderer* text,
                                         int row, const char* labelKey)
{
    auto c = std::make_unique<UICheckbox>();
    c->position = {kControlX, rowY(row, theme) + scaled(10.0f, theme)};
    c->size = {24.0f, 20.0f};
    c->theme = &theme;
    c->textRenderer = text;
    c->accessible().label = std::string(tr(labelKey));
    return c;
}

std::unique_ptr<UISlider> makeSlider(const UITheme& theme, TextRenderer* text, int row,
                                     const char* labelKey, float minValue, float maxValue,
                                     float keyStep, std::function<std::string(float)> formatter)
{
    auto s = std::make_unique<UISlider>();
    s->position = {kControlX, rowY(row, theme)};
    s->size = {420.0f, 44.0f};
    s->minValue = minValue;
    s->maxValue = maxValue;
    s->value = minValue;
    s->keyStep = keyStep;
    s->formatter = std::move(formatter);
    s->theme = &theme;
    s->textRenderer = text;
    s->accessible().label = std::string(tr(labelKey));
    return s;
}

std::string percentText(float value)
{
    return UISlider::composePercentText(static_cast<int>(std::lround(value * 100.0f)));
}

} // namespace SettingsPageLayout

} // namespace Vestige
