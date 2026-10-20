// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file controls_settings_page.h
/// @brief 3D_E-0759 — the players' mouse and gamepad options on the Settings
///        screen. Key rebinding is not here (3D_E-0781).
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
class UIElement;
class UISlider;
struct UITheme;

/// @brief The page's controls, for tests. Owned by the canvas.
struct ControlsSettingsPage
{
    UISlider*   mouseSensitivity  = nullptr;
    UICheckbox* invertMouse       = nullptr;
    UISlider*   leftStickDeadZone = nullptr;
    UISlider*   rightStickDeadZone = nullptr;
    UIElement*  root              = nullptr;  ///< Holds every control; hidden off this page.
};

/// @brief Adds the controls options to the Settings content area, bound to
///        @a editor, inside one container element (`root`). Before drawing,
///        they copy their state from `editor.pending()` each frame.
ControlsSettingsPage buildControlsSettingsPage(UICanvas& canvas, const UITheme& theme,
                                               TextRenderer* text, SettingsEditor& editor);

/// @brief Every localisation key the page uses.
std::vector<std::string_view> controlsSettingsPageKeys();

} // namespace Vestige
