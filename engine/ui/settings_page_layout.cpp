// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/settings_page_layout.h"

#include "localization/localization_service.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_label.h"
#include "ui/ui_panel.h"
#include "ui/ui_slider.h"
#include "ui/ui_theme.h"

#include <cmath>
#include <utility>

namespace Vestige
{

namespace SettingsPageLayout
{

float rowY(int row)
{
    return kFirstRow + kRowStep * static_cast<float>(row);
}

std::unique_ptr<UIPanel> makePageRoot()
{
    auto root = std::make_unique<UIPanel>();
    root->position = {0.0f, 0.0f};
    root->size = {1920.0f, 1080.0f};
    root->anchor = Anchor::TOP_LEFT;
    root->backgroundColor = {0.0f, 0.0f, 0.0f, 0.0f};
    root->interactive = false;
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
        label->position = {kLabelX, rowY(static_cast<int>(row)) + 10.0f};
        label->scale = 0.34f;
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
    d->position = {kControlX, rowY(row)};
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
    c->position = {kControlX, rowY(row) + 10.0f};
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
    s->position = {kControlX, rowY(row)};
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
