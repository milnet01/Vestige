// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file menu_prefabs.h
/// @brief Factory functions that populate `UICanvas` instances with the
///        three menu layouts from the `vestige-ui-hud-inworld` Claude Design
///        hand-off.
///
/// Each builder takes a fresh `UICanvas`, the active theme, and the engine's
/// text renderer; populates the canvas with positioned widgets matching the
/// design's React layouts. Callers wire signals (button clicks, slider
/// changes) after construction.
///
/// **Status:** Main and Pause are full layout matches. Settings ships the
/// chrome (header / category sidebar / content frame / footer) — populating
/// the per-category controls is a per-game integration concern (the engine
/// can't decide which settings exist for any given game project).
#pragma once

#include "ui/ui_canvas.h"
#include "ui/ui_theme.h"

#include <functional>

namespace Vestige
{

class TextRenderer;
class UISystem;

/// @brief Builds the main-menu canvas (cold-start screen).
///
/// Layout: top chrome rule, "VESTIGE" wordmark + 5-item button list on the
/// left, continue card on the right, footer with keyboard shortcut hints.
/// Buttons are: New Walkthrough / Continue / Templates / Settings / Quit.
/// Wire the per-button onClick signals after this function returns.
void buildMainMenu(UICanvas& canvas,
                    const UITheme& theme,
                    TextRenderer* textRenderer);

/// @brief Phase 10 slice 12.2 overload. Builds the layout **and** wires
///        each menu button's `onClick` signal to the matching
///        `UISystem::applyIntent(...)` transition so the menu is
///        interactive as soon as it's constructed:
///        New Walkthrough → `NewWalkthrough`, Continue → `Continue`,
///        Settings → `OpenSettings`, Quit → `QuitToDesktop`.
///        Templates has no intent (per-game concern) and stays inert.
void buildMainMenu(UICanvas& canvas,
                    const UITheme& theme,
                    TextRenderer* textRenderer,
                    UISystem& uiSystem);

/// @brief Builds the pause-menu canvas (in-game overlay).
///
/// Layout: centred 720-px-wide panel with corner brackets, "PAUSED" caption,
/// "The walk is held." headline, and 7 buttons (Resume / Save / Save As /
/// Load / Settings / Quit to Main / Quit to Desktop). The pause scrim and
/// pause-bars decoration are drawn by the engine render pass — this builder
/// only populates the panel and its buttons.
void buildPauseMenu(UICanvas& canvas,
                     const UITheme& theme,
                     TextRenderer* textRenderer);

/// @brief Phase 10 slice 12.2 overload. Builds the layout **and** wires
///        each pause button's `onClick` signal to `UISystem::applyIntent`:
///        Resume → `Resume`, Settings → `OpenSettings`, Quit to Main →
///        `QuitToMain`, Quit to Desktop → `QuitToDesktop`. Save / Save As /
///        Load stay inert (per-game save-system concern).
void buildPauseMenu(UICanvas& canvas,
                     const UITheme& theme,
                     TextRenderer* textRenderer,
                     UISystem& uiSystem);

/// @brief Builds the settings-menu chrome (modal full-bleed panel).
///
/// Layout: 28-px-padded header with "Settings" title + ESC close button,
/// 300-px-wide category sidebar (Display / Audio / Controls / Gameplay /
/// Accessibility) and content area on the right, footer with
/// Restore Defaults / Revert / Apply buttons + dirty indicator.
///
/// **Per-category controls are not added here** — game projects extend the
/// content area by appending widgets to the canvas after this function
/// returns. The chrome is provided so every game's settings menu shares the
/// same framing language.
void buildSettingsMenu(UICanvas& canvas,
                        const UITheme& theme,
                        TextRenderer* textRenderer);

/// @brief Phase 10 slice 12.2 overload. Builds the chrome **and** wires
///        the header `ESC CLOSE` button's `onClick` signal to
///        `UISystem::applyIntent(CloseSettings)`. Restore Defaults /
///        Revert / Apply remain inert — per-game concerns.
void buildSettingsMenu(UICanvas& canvas,
                        const UITheme& theme,
                        TextRenderer* textRenderer,
                        UISystem& uiSystem);

/// @brief 3D_E-0035 — what the Settings footer buttons do.
struct SettingsMenuActions
{
    std::function<void()> apply;            ///< Save the pending settings.
    std::function<void()> revert;           ///< Drop the unsaved changes.
    std::function<void()> restoreDefaults;  ///< Reset the page's options.
    std::function<bool()> isDirty;          ///< True while there are unsaved changes.
};

/// @brief 3D_E-0035 overload: the chrome with working footer buttons. Apply
///        and Revert are enabled, and the status reads `ui.settings.unsaved`,
///        only while `actions.isDirty()` is true; this updates each frame.
void buildSettingsMenu(UICanvas& canvas,
                        const UITheme& theme,
                        TextRenderer* textRenderer,
                        UISystem& uiSystem,
                        const SettingsMenuActions& actions);

/// @brief Phase 10 slice 12.4 — builds the default HUD composition for
///        first-person walkthrough gameplay.
///
/// The canvas is populated with four root elements, in this order:
///   -# `UICrosshair` at `CENTER`, sized to `UITheme::crosshairLength` /
///      `crosshairThickness`, coloured with `theme.crosshair`.
///   -# `UIFpsCounter` at `TOP_LEFT`, **hidden by default** — game code
///      toggles `visible` from a debug flag.
///   -# `UIPanel` at `BOTTOM_CENTER` (4 body-lines above the bottom edge),
///      a transparent anchor container game code attaches interaction
///      prompts to. No visual fill — `backgroundColor.a = 0`.
///   -# `UIPanel` at `TOP_RIGHT` holding three `UINotificationToast`
///      children, pre-sized and stacked vertically. Toasts start
///      invisible (`alpha = 0`); the notification queue drives them.
///
/// Game-specific overlays (health bar, inventory, minimap) layer on
/// top — this prefab provides the walkthrough baseline that every
/// Vestige game shares.
///
/// @note Calling this on a non-empty canvas appends elements rather than
///       replacing them — caller is responsible for `canvas.clear()`
///       when rebuilding.
void buildDefaultHud(UICanvas& canvas,
                      const UITheme& theme,
                      TextRenderer* textRenderer,
                      UISystem& uiSystem);

} // namespace Vestige
