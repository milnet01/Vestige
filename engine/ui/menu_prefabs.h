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

namespace Vestige
{

class TextRenderer;

/// @brief Builds the main-menu canvas (cold-start screen).
///
/// Layout: top chrome rule, "VESTIGE" wordmark + 5-item button list on the
/// left, continue card on the right, footer with keyboard shortcut hints.
/// Buttons are: New Walkthrough / Continue / Templates / Settings / Quit.
/// Wire the per-button onClick signals after this function returns.
void buildMainMenu(UICanvas& canvas,
                    const UITheme& theme,
                    TextRenderer* textRenderer);

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

} // namespace Vestige
