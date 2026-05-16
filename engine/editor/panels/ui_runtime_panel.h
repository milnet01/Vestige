// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_runtime_panel.h
/// @brief Phase 10 slice 12.5 — editor surface over the in-game UI
///        runtime: the `GameScreen` state machine, menu prefab
///        previews, default HUD element toggles, and live
///        accessibility composition.
///
/// Distinct from `UILayoutPanel` (phase 9C), which inspects an
/// arbitrary `UICanvas` element tree. `UIRuntimePanel` is specifically
/// about the runtime state — *which screen is showing*, *how the
/// screen-stack got here*, *what the default HUD looks like*, and
/// *how accessibility transforms compose*.
///
/// Four tabs, mirroring the `AudioPanel` layout discipline:
///   - **State** — current root / modal-top screen readout, recent
///                 screen-push log (20 most recent transitions), and
///                 buttons that fire every `GameScreenIntent` so the
///                 editor can drive the state machine manually.
///   - **Menus** — rebuild a preview of MainMenu / Pause / Settings
///                 using the real menu prefabs into an editor-owned
///                 canvas. An offscreen render path composites the
///                 preview into the panel (signed off 2026-04-21 to
///                 keep the live scene clean).
///   - **HUD**   — toggle each default-HUD element's visibility
///                 (crosshair / FPS counter / interaction anchor /
///                 notification stack).
///   - **Accessibility** — compose the three transforms (scale preset
///                 / high contrast / reduced motion) against the
///                 engine's live `UISystem` so users preview their
///                 accessibility choices without re-launching the game.
///
/// State is exposed via getters so unit tests can exercise every
/// non-ImGui path without an ImGui context — same discipline as
/// `AudioPanel` and `NavigationPanel`.
#pragma once

#include "editor/panels/i_panel.h"
#include "ui/game_screen.h"
#include "ui/ui_canvas.h"
#include "ui/ui_theme.h"

#include <array>
#include <cstddef>
#include <vector>

namespace Vestige
{

class UISystem;
class TextRenderer;

/// @brief Which menu prefab the `Menus` tab is previewing.
enum class UIRuntimePanelMenu
{
    MainMenu,
    Paused,
    Settings,
};

/// @brief Editor panel for in-game UI runtime inspection + accessibility
///        composition. See file docstring for tab breakdown.
class UIRuntimePanel : public IPanel
{
public:
    const char* displayName() const override { return "UI Runtime"; }

    /// Cap on the screen-push log. 20 entries balances scrollback
    /// usefulness against clutter; matches the conservative end of the
    /// `HistoryPanel` depth.
    static constexpr std::size_t SCREEN_LOG_CAPACITY = 20;

    /// Default HUD elements that can be individually toggled on the
    /// `HUD` tab. Indices match the order `buildDefaultHud` adds them
    /// to the canvas.
    enum class HudElement : std::size_t
    {
        Crosshair           = 0,
        FpsCounter          = 1,
        InteractionAnchor   = 2,
        NotificationStack   = 3,
    };
    static constexpr std::size_t HUD_ELEMENT_COUNT = 4;

    UIRuntimePanel();

    /// @brief Draws the panel inside its own ImGui window. `uiSystem`
    ///        may be null — tabs that depend on it show an empty-state
    ///        banner and skip their controls.
    void draw(UISystem* uiSystem);

    // -- Open / close ---------------------------------------------------

    bool isOpen() const override { return m_open; }
    void setOpen(bool open) override { m_open = open; }
    void toggleOpen() { m_open = !m_open; }

    // -- Screen-push log ------------------------------------------------

    /// @brief Records a screen transition for the scrollback display.
    ///        Capped at `SCREEN_LOG_CAPACITY` — oldest entries are
    ///        evicted to keep the log bounded.
    void recordScreenTransition(GameScreen from, GameScreen to,
                                GameScreenIntent intent);

    /// @brief One entry in the scrollback.
    struct ScreenLogEntry
    {
        GameScreen       from   = GameScreen::None;
        GameScreen       to     = GameScreen::None;
        GameScreenIntent intent = GameScreenIntent::OpenMainMenu;
    };

    /// @brief Oldest-first list of recent transitions.
    const std::vector<ScreenLogEntry>& screenLog() const { return m_screenLog; }

    /// @brief Clears the scrollback.
    void clearScreenLog() { m_screenLog.clear(); }

    // -- Menu preview ---------------------------------------------------

    /// @brief Selects which menu prefab the `Menus` tab previews.
    ///        Triggers a canvas rebuild via `refreshMenuPreview` so the
    ///        preview canvas always reflects the latest selection.
    void setMenuPreview(UIRuntimePanelMenu menu);

    /// @brief Currently selected menu preview.
    UIRuntimePanelMenu menuPreview() const { return m_menuPreview; }

    /// @brief Rebuilds the preview canvas against @a theme using the
    ///        menu prefab matching `menuPreview()`. Called automatically
    ///        on `setMenuPreview`; tests and the panel's draw path may
    ///        call it to force a rebuild (e.g. after a theme change).
    ///
    /// `textRenderer` may be null — the menu prefabs tolerate a null
    /// renderer by skipping glyph draws; the structural element tree
    /// is still populated.
    void refreshMenuPreview(const UITheme& theme, TextRenderer* textRenderer);

    /// @brief The canvas holding the current menu preview.
    UICanvas& previewCanvas() { return m_previewCanvas; }
    const UICanvas& previewCanvas() const { return m_previewCanvas; }

    // -- HUD element toggles -------------------------------------------

    /// @brief Read the visibility toggle for a HUD element.
    bool isHudElementVisible(HudElement element) const;

    /// @brief Mutate the visibility toggle for a HUD element. Applying
    ///        the toggle to the live `UISystem`'s HUD canvas happens
    ///        inside the `HUD` tab draw path — the panel merely
    ///        stores the preference.
    void setHudElementVisible(HudElement element, bool visible);

    /// @brief Writes every `m_hudVisible[i]` onto the `uiSystem`'s
    ///        root canvas's top-level elements. No-op when the root
    ///        screen is not `Playing` (the HUD prefab only populates
    ///        that canvas). Returns the number of elements written.
    std::size_t applyHudTogglesTo(UISystem& uiSystem) const;

private:
    void drawStateTab(UISystem* uiSystem);
    void drawMenusTab(UISystem* uiSystem);
    void drawHudTab(UISystem* uiSystem);
    void drawAccessibilityTab(UISystem* uiSystem);

    bool m_open = false;

    std::vector<ScreenLogEntry> m_screenLog;

    UIRuntimePanelMenu m_menuPreview = UIRuntimePanelMenu::MainMenu;
    UICanvas m_previewCanvas;

    // All HUD elements are visible by default (matching the prefab's
    // baseline, except the FPS counter which the prefab hides — the
    // panel still carries a `true` here and the user can flip it on).
    std::array<bool, HUD_ELEMENT_COUNT> m_hudVisible = {true, true, true, true};
};

} // namespace Vestige
