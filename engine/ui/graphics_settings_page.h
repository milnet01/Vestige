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
};

/// @brief Adds the graphics controls to the Settings content area, bound to
///        @a editor. Before drawing, they copy their state from
///        `editor.pending()` each frame, so a Revert or a preset shows at once.
GraphicsSettingsPage buildGraphicsSettingsPage(UICanvas& canvas, const UITheme& theme,
                                               TextRenderer* text, SettingsEditor& editor);

/// @brief Registers the Settings builder (chrome with working footer, and
///        this page) and connects `onModalPopped`: when Settings closes with
///        unsaved changes, `editor.revert()`. The engine and the tests both
///        call this, so they run the same wiring.
void wireSettingsScreen(UISystem& ui, SettingsEditor& editor,
                        const std::filesystem::path& settingsPath);

/// @brief Every localisation key the page and the footer use (INV-10).
std::vector<std::string_view> settingsPageKeys();

} // namespace Vestige
