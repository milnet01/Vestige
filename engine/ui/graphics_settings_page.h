// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file graphics_settings_page.h
/// @brief 3D_E-0035 — the players' graphics options on the Settings screen.
///
/// Spec: docs/specs/3D_E-0035-graphics-settings-menu.md. Every control edits
/// `SettingsEditor::pending()` through `mutate`, so a change previews live;
/// the footer's Apply saves it, Revert or closing Settings drops it.
#pragma once

#include <filesystem>
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
class UISystem;
struct UITheme;

/// @brief The page's controls, for tests. Owned by the canvas.
struct GraphicsSettingsPage
{
    UIDropdown* preset           = nullptr;
    UISlider*   renderScale      = nullptr;
    UIDropdown* antiAlias        = nullptr;
    UICheckbox* ambientOcclusion = nullptr;
    UICheckbox* bloom            = nullptr;
    UICheckbox* volumetrics      = nullptr;
    UIDropdown* terrainDetail    = nullptr;
    UIDropdown* foliageDetail    = nullptr;
    UIDropdown* grassDetail      = nullptr;
    UIDropdown* treeDetail       = nullptr;
    UICheckbox* vsync            = nullptr;
    UIDropdown* windowMode       = nullptr;
    UIElement*  root             = nullptr;  ///< Holds every control; hidden off this page.
};

/// @brief Adds the graphics controls to the Settings content area, bound to
///        @a editor, inside one container element (`root`). Before drawing, they copy their state from
///        `editor.pending()` each frame, so a Revert or a preset shows at once.
GraphicsSettingsPage buildGraphicsSettingsPage(UICanvas& canvas, const UITheme& theme,
                                               TextRenderer* text, SettingsEditor& editor);

/// @brief Registers the Settings builder and connects `onModalPopped`: when
///        Settings closes with unsaved changes, `editor.revert()`. The engine
///        and the tests both call this, so they run the same wiring.
///
/// The builder adds the chrome with a working footer and a sidebar of
/// Display, Audio, Controls and Accessibility (3D_E-0759), then each page in
/// its own container. Choosing a category shows its page and hides the
/// rest; Settings opens on Display. Restore defaults resets only the page on
/// show.
void wireSettingsScreen(UISystem& ui, SettingsEditor& editor,
                        const std::filesystem::path& settingsPath);

/// @brief Every localisation key the Settings pages, the sidebar and the
///        footer use (INV-10).
std::vector<std::string_view> settingsPageKeys();

} // namespace Vestige
