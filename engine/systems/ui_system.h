// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_system.h
/// @brief Domain system for in-game UI and HUD rendering.
#pragma once

#include "core/i_system.h"
#include "ui/sprite_batch_renderer.h"
#include "ui/ui_canvas.h"

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

    /// @brief Whether the UI wants to capture mouse input (cursor over interactive element).
    bool wantsCaptureInput() const { return m_wantsCaptureInput; }

private:
    static inline const std::string m_name = "UI";
    SpriteBatchRenderer m_spriteBatch;
    UICanvas m_canvas;
    Engine* m_engine = nullptr;
    bool m_wantsCaptureInput = false;
};

} // namespace Vestige
