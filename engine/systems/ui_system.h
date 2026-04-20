// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_system.h
/// @brief Domain system for in-game UI and HUD rendering.
#pragma once

#include "core/i_system.h"
#include "ui/sprite_batch_renderer.h"
#include "ui/ui_canvas.h"
#include "ui/ui_theme.h"

#include <glm/glm.hpp>

#include <string>

namespace Vestige
{

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
};

} // namespace Vestige
