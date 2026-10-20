// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_settings_page.h
/// @brief 3D_E-0759 — the players' audio options on the Settings screen.
///
/// Every control edits `SettingsEditor::pending()` through `mutate`, so a
/// change previews live; the footer's Apply saves it, Revert or closing
/// Settings drops it.
#pragma once

#include <array>
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
struct AudioSettingsPage
{
    /// One volume per mixer bus, in `AudioSettings::busGains` order:
    /// Master, Music, Voice, Effects, Ambient, Interface.
    std::array<UISlider*, 6> volumes{};
    UICheckbox* headphone3d = nullptr;
    UIElement*  root        = nullptr;  ///< Holds every control; hidden off this page.
};

/// @brief Adds the audio controls to the Settings content area, bound to
///        @a editor, inside one container element (`root`). Before drawing,
///        they copy their state from `editor.pending()` each frame.
AudioSettingsPage buildAudioSettingsPage(UICanvas& canvas, const UITheme& theme,
                                         TextRenderer* text, SettingsEditor& editor);

/// @brief Every localisation key the page uses.
std::vector<std::string_view> audioSettingsPageKeys();

} // namespace Vestige
