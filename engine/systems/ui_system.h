// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_system.h
/// @brief Domain system for in-game UI and HUD rendering.
#pragma once

#include "core/i_system.h"
#include "ui/game_screen.h"
#include "ui/sprite_batch_renderer.h"
#include "ui/ui_canvas.h"
#include "ui/ui_notification_toast.h"
#include "ui/ui_signal.h"
#include "ui/ui_theme.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class TextRenderer;

/// @brief Manages in-game UI rendering (separate from ImGui editor).
///
/// Owns the SpriteBatchRenderer for batched 2D quad drawing and a UICanvas
/// for element hierarchy. Renders as a 2D overlay after the 3D scene in
/// PLAY mode. The editor (EDIT mode) uses ImGui instead.
class UISystem : public ISystem
{
public:
    UISystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;

    // -- Accessors --
    SpriteBatchRenderer& getSpriteBatchRenderer() { return m_spriteBatch; }
    const SpriteBatchRenderer& getSpriteBatchRenderer() const { return m_spriteBatch; }
    UICanvas& getCanvas() { return m_canvas; }
    const UICanvas& getCanvas() const { return m_canvas; }

    /// @brief Renders the UI overlay. Called from the engine render loop.
    /// @param screenWidth Viewport width.
    /// @param screenHeight Viewport height.
    void renderUI(int screenWidth, int screenHeight);

    /// @brief Whether the UI wants to capture input.
    ///
    /// Returns true if **either**:
    /// (a) `setModalCapture(true)` is in effect (e.g. a pause menu is open), or
    /// (b) the cursor is over an interactive UI element (set via
    ///     `updateMouseHit()`).
    ///
    /// Game input handlers should consult this each frame and skip movement /
    /// look / fire bindings when it returns true so a click on a UI button
    /// does not also trigger a fire action.
    bool wantsCaptureInput() const { return m_modalCapture || m_cursorOverInteractive; }

    /// @brief Pins input capture on for modal UI (pause menu, dialog, etc.).
    /// @param modal true to suppress all game input until cleared.
    void setModalCapture(bool modal) { m_modalCapture = modal; }

    /// @brief True when modal capture is currently active.
    bool isModalCapture() const { return m_modalCapture; }

    /// @brief Updates the "cursor over interactive element" cache.
    /// Call once per frame from the engine input loop with the current cursor.
    /// @param cursor Screen-space cursor position (pixels).
    /// @param screenWidth Viewport width.
    /// @param screenHeight Viewport height.
    void updateMouseHit(const glm::vec2& cursor, int screenWidth, int screenHeight);

    /// @brief Returns the active theme (mutable so game code can override per-field).
    ///
    /// Note: a subsequent call to `setScalePreset` / `setHighContrastMode` /
    /// `setBaseTheme` rebuilds the active theme from the stored base, which
    /// discards per-field overrides applied through this accessor. To keep
    /// overrides across accessibility toggles, bake them into the base
    /// theme via `setBaseTheme`.
    UITheme& getTheme() { return m_theme; }
    const UITheme& getTheme() const { return m_theme; }

    /// @brief Returns the stored base theme — the unscaled, non-high-contrast
    ///        palette that every accessibility transform composes on top of.
    const UITheme& getBaseTheme() const { return m_baseTheme; }

    /// @brief Replaces the base theme. Rebuilds the active theme immediately
    ///        so current scale / high-contrast selections stay applied.
    void setBaseTheme(const UITheme& base);

    /// @brief Applies a UI scale preset (Phase 10 accessibility).
    ///
    /// 1.0× / 1.25× / 1.5× / 2.0× of every pixel-size field on the theme.
    /// Partially-sighted users should select 1.5× (the minimum recommended
    /// in Phase 10) or 2.0×.
    void setScalePreset(UIScalePreset preset);

    /// @brief Returns the currently selected scale preset.
    UIScalePreset getScalePreset() const { return m_scalePreset; }

    /// @brief Toggles high-contrast UI mode (Phase 10 accessibility).
    ///
    /// Swaps the palette for a pure-black-on-white register with saturated
    /// accent colours. Sizing stays under scale-preset control so users can
    /// combine high-contrast + 2.0× if needed.
    void setHighContrastMode(bool enabled);

    /// @brief True when high-contrast mode is currently applied.
    bool isHighContrastMode() const { return m_highContrast; }

    /// @brief Toggles reduced-motion UI mode (Phase 10 photosensitivity
    ///        safe mode).
    ///
    /// Zeros `transitionDuration` on the active theme so UI transitions
    /// snap instead of easing. Composes with `setScalePreset` and
    /// `setHighContrastMode` so users can run any combination of the
    /// three accessibility toggles simultaneously.
    void setReducedMotion(bool enabled);

    /// @brief True when reduced-motion mode is currently applied.
    bool isReducedMotion() const { return m_reducedMotion; }

    /// @brief Phase 10 slice 13.3: apply the three accessibility
    ///        toggles in one rebuild.
    ///
    /// `setScalePreset`, `setHighContrastMode`, and `setReducedMotion`
    /// each call `rebuildTheme()` individually — convenient for the
    /// editor's live toggles but wasteful when the Settings apply path
    /// wants to push all three values at once (fresh load, Apply, or
    /// Restore Defaults). This batch setter updates the three fields
    /// then runs `rebuildTheme()` exactly once.
    ///
    /// Equivalent to calling the three individual setters; use it when
    /// you have all three values up front.
    void applyAccessibilityBatch(UIScalePreset scale,
                                  bool highContrast,
                                  bool reducedMotion);

    // -- Phase 10 slice 12.2: game-screen state machine + modal stack --

    /// @brief Injects the text renderer used by screen builders.
    ///
    /// Builders populate the canvas with labels that need a `TextRenderer`
    /// to rasterize. Engine wires this to `Renderer::getTextRenderer()`
    /// during initialize. Tests may leave it null — builders tolerate
    /// null by skipping glyph rendering.
    void setTextRenderer(TextRenderer* textRenderer) { m_textRenderer = textRenderer; }

    /// @brief Builder callback type. Receives the target canvas + theme +
    ///        text renderer + back-reference to this UISystem (so the
    ///        prefab can wire button onClick signals to `applyIntent`).
    ///        Ownership of widgets stays with the canvas.
    using ScreenBuilder = std::function<void(UICanvas&, const UITheme&,
                                             TextRenderer*, UISystem&)>;

    /// @brief Register a custom builder for a given screen. Overrides the
    ///        default prefab (e.g. game code can swap `buildPauseMenu` for
    ///        a studio-branded pause screen without touching engine code).
    ///        Pass an empty `ScreenBuilder{}` to clear a previous override.
    void setScreenBuilder(GameScreen screen, ScreenBuilder builder);

    /// @brief Sets the root screen. Clears any stacked modals and rebuilds
    ///        the canvas via the registered prefab factory. Passing
    ///        `GameScreen::None` clears the canvas — used by the editor /
    ///        headless harnesses that drive widgets directly.
    void setRootScreen(GameScreen screen);

    /// @brief Pushes a modal screen on top of the root (Settings, confirm
    ///        dialog). The modal canvas is built and rendered on top of
    ///        the root canvas. Modal capture is enabled automatically.
    void pushModalScreen(GameScreen screen);

    /// @brief Pops the top modal. If the stack is empty, this is a no-op.
    ///        When the last modal is popped, modal capture clears.
    void popModalScreen();

    /// @brief Returns the current root screen. Defaults to `GameScreen::None`.
    GameScreen getRootScreen() const { return m_rootScreen; }

    /// @brief Returns the top-of-stack modal screen, or `GameScreen::None`
    ///        if no modal is active.
    GameScreen getTopModalScreen() const
    {
        return m_modalStack.empty() ? GameScreen::None : m_modalStack.back();
    }

    /// @brief Returns the effective current screen — top modal if any,
    ///        otherwise the root.
    GameScreen getCurrentScreen() const
    {
        return m_modalStack.empty() ? m_rootScreen : m_modalStack.back();
    }

    /// @brief Applies an intent via the pure state machine, then reconciles
    ///        the canvas to match. This is the call site game code should
    ///        use — buttons, hotkeys, engine events all funnel through it.
    ///
    /// Routing:
    /// - `OpenSettings` from a non-modal screen → pushes `Settings` modal.
    /// - `CloseSettings` while a modal is on top → pops the modal
    ///   (preserves the root, matching the design's "return to previous").
    /// - Any other state change → replaces the root screen.
    /// - No-op intents (rejected by the pure function) return silently.
    void applyIntent(GameScreenIntent intent);

    /// @brief Access to the modal canvas, for introspection and tests.
    UICanvas& getModalCanvas() { return m_modalCanvas; }
    const UICanvas& getModalCanvas() const { return m_modalCanvas; }

    // -- Phase 10 slice 12.4: notification queue --

    /// @brief Transient notification queue driving the top-right toast
    ///        stack populated by `buildDefaultHud`. Game events push into
    ///        this queue (`getNotifications().push(...)`) and `UISystem::update`
    ///        advances it each frame using the active theme's
    ///        `transitionDuration` as the fade-in / fade-out ramp.
    NotificationQueue& getNotifications() { return m_notifications; }
    const NotificationQueue& getNotifications() const { return m_notifications; }

    // Signals — game code hooks without subclassing.
    Signal<GameScreen> onRootScreenChanged;
    Signal<GameScreen> onModalPushed;
    Signal<GameScreen> onModalPopped;

private:
    /// @brief Returns the registered builder for @a screen, falling back to
    ///        the built-in default (menu_prefabs::build*). Returns an empty
    ///        function if the screen has no default (e.g. `None`, `Exiting`).
    ScreenBuilder resolveBuilder(GameScreen screen) const;

    /// @brief Dispatches to the built-in `menu_prefabs` default for the
    ///        given screen. Centralised so `setScreenBuilder(…, empty)`
    ///        falls back cleanly.
    static ScreenBuilder defaultBuilderFor(GameScreen screen);

private:
    /// @brief Recomputes `m_theme` from `m_baseTheme` with the current
    ///        scale preset, high-contrast, and reduced-motion flags
    ///        applied. Idempotent.
    void rebuildTheme();

    static inline const std::string m_name = "UI";
    SpriteBatchRenderer m_spriteBatch;
    UICanvas m_canvas;
    Engine* m_engine = nullptr;
    UITheme m_baseTheme = UITheme::defaultTheme();
    UITheme m_theme     = UITheme::defaultTheme();
    UIScalePreset m_scalePreset = UIScalePreset::X1_0;
    bool m_highContrast          = false;
    bool m_reducedMotion         = false;
    bool m_modalCapture          = false;
    bool m_cursorOverInteractive = false;
    // Kept for ABI continuity but no longer the canonical capture source —
    // the union of m_modalCapture and m_cursorOverInteractive is what
    // wantsCaptureInput() returns.
    bool m_wantsCaptureInput     = false;

    // Phase 10 slice 12.2 — screen state machine + modal stack.
    TextRenderer*                             m_textRenderer = nullptr;
    GameScreen                                m_rootScreen   = GameScreen::None;
    UICanvas                                  m_modalCanvas;
    std::vector<GameScreen>                   m_modalStack;
    std::unordered_map<GameScreen, ScreenBuilder> m_screenBuilders;

    // Phase 10 slice 12.4 — transient notification queue (toast stack).
    NotificationQueue m_notifications;
};

} // namespace Vestige
