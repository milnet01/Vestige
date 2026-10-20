// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file accessibility_settings_page.h
/// @brief 3D_E-0759 — the players' accessibility options on the Settings
///        screen. Depth of field and motion blur are not offered: the engine
///        does not ship those effects.
///
/// Every control edits `SettingsEditor::pending()` through `mutate`, so a
/// change previews live; the footer's Apply saves it, Revert or closing
/// Settings drops it.
#pragma once

#include <string_view>
#include <vector>

namespace Vestige
{

class SettingsEditor;
class TextRenderer;
class UICanvas;
class UICheckbox;
class UIDropdown;
class UIElement;
class UISlider;
struct UITheme;

/// @brief The page's controls, for tests. Owned by the canvas.
struct AccessibilitySettingsPage
{
    UIDropdown* interfaceScale     = nullptr;
    UICheckbox* highContrast       = nullptr;
    UICheckbox* reducedMotion      = nullptr;
    UICheckbox* subtitles          = nullptr;
    UIDropdown* subtitleSize       = nullptr;
    UIDropdown* colourVisionFilter = nullptr;
    UICheckbox* photosensitiveSafe = nullptr;
    UICheckbox* fog                = nullptr;
    UISlider*   fogIntensity       = nullptr;
    UICheckbox* volumetricFog      = nullptr;
    UICheckbox* godRays            = nullptr;
    UICheckbox* dynamicLighting    = nullptr;
    UICheckbox* freezeLighting     = nullptr;
    UIElement*  root               = nullptr;  ///< Holds every control; hidden off this page.
};

/// @brief Adds the accessibility controls to the Settings content area,
///        bound to @a editor, inside one container element (`root`). Before
///        drawing, they copy their state from `editor.pending()` each frame.
AccessibilitySettingsPage buildAccessibilitySettingsPage(UICanvas& canvas, const UITheme& theme,
                                                         TextRenderer* text,
                                                         SettingsEditor& editor);

/// @brief Every localisation key the page uses.
std::vector<std::string_view> accessibilitySettingsPageKeys();

} // namespace Vestige
