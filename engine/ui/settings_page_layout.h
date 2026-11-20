// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_page_layout.h
/// @brief 3D_E-0759 — the row layout and control makers the player Settings
///        pages share (Display, Audio, Controls, Accessibility).
///
/// Internal to the pages: each page puts a label on the left of a row and its
/// control on the right, inside one container element that the sidebar shows
/// or hides.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace Vestige
{

class TextRenderer;
class UICheckbox;
class UIDropdown;
class UIElement;
class UIScrollRows;
class UISlider;
struct UITheme;

namespace SettingsPageLayout
{

// Content area of the Settings chrome (menu_prefabs.cpp): right of the
// sidebar, above the footer rule.
constexpr float kLabelX   = 468.0f;
constexpr float kControlX = 1000.0f;
constexpr float kFirstRow = 236.0f;
// The footer rule (menu_prefabs.cpp: modalY + modalH - 70 - 16). Rows end above it.
constexpr float kRowsBottom = 914.0f;
// Right of the widest control (a slider and its value text), inside the modal.
constexpr float kScrollBarX = 1720.0f;

/// @brief Top of one row to the top of the next. It grows with the theme's
///        dropdown height, the tallest control a row holds, so rows never
///        overlap at a larger interface scale.
float rowStep(const UITheme& theme);

/// @brief The top of row @a row with every row showing.
float rowY(int row, const UITheme& theme);

/// @brief The page's container, at the canvas origin, so a child's position
///        is the same as on the canvas itself. Not interactive. It shows the
///        rows that fit above the footer rule and scrolls to the rest.
std::unique_ptr<UIScrollRows> makePageRoot(const UITheme& theme, std::size_t rowCount);

/// @brief Adds one label per row, in order, to @a root.
void addRowLabels(UIElement& root, const char* const* keys, std::size_t count,
                  const UITheme& theme, TextRenderer* text);

/// @brief A dropdown whose options are the string-table @a keys.
std::unique_ptr<UIDropdown> makeDropdown(const char* const* keys, std::size_t count,
                                         const UITheme& theme, TextRenderer* text,
                                         int row, const char* labelKey);

template <std::size_t N>
std::unique_ptr<UIDropdown> makeDropdown(const char* const (&keys)[N], const UITheme& theme,
                                         TextRenderer* text, int row, const char* labelKey)
{
    return makeDropdown(keys, N, theme, text, row, labelKey);
}

std::unique_ptr<UICheckbox> makeCheckbox(const UITheme& theme, TextRenderer* text,
                                         int row, const char* labelKey);

/// @brief A slider from @a minValue to @a maxValue that Left / Right move by
///        @a keyStep, showing its value through @a formatter.
std::unique_ptr<UISlider> makeSlider(const UITheme& theme, TextRenderer* text, int row,
                                     const char* labelKey, float minValue, float maxValue,
                                     float keyStep, std::function<std::string(float)> formatter);

/// @brief Shows 0..1 as a whole percent ("80 %").
std::string percentText(float value);

/// @brief The index of @a value in @a values, or 0 when absent.
template <typename T, std::size_t N>
int indexOf(const T (&values)[N], const T& value)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (values[i] == value)
        {
            return static_cast<int>(i);
        }
    }
    return 0;
}

} // namespace SettingsPageLayout

} // namespace Vestige
