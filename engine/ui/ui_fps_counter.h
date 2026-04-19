// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_fps_counter.h
/// @brief Live frame-rate readout HUD widget.
#pragma once

#include "ui/ui_element.h"

#include <glm/glm.hpp>

namespace Vestige
{

class TextRenderer;

/// @brief Displays smoothed FPS as text using `TextRenderer`.
///
/// Caller drives the value via `tick(deltaTimeSeconds)` each frame; the
/// widget keeps an exponential moving average to avoid flicker. Display
/// format defaults to `"%.0f FPS"`. Set `textRenderer` (UISystem typically
/// does this for you).
class UIFpsCounter : public UIElement
{
public:
    UIFpsCounter() { interactive = false; size = {120.0f, 24.0f}; }

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief Feeds a frame's delta-time into the smoothed FPS estimate.
    /// Safe to call with deltaSeconds == 0 (no-op).
    void tick(float deltaSeconds);

    /// @brief Returns the current smoothed FPS estimate.
    float getSmoothedFps() const { return m_smoothedFps; }

    /// @brief Text colour.
    glm::vec3 color = {1.0f, 1.0f, 1.0f};

    /// @brief Text scale factor.
    float scale = 0.5f;

    /// @brief Smoothing weight in [0, 1]. 1 = no smoothing, 0.05 = heavy smoothing.
    float smoothing = 0.1f;

    /// @brief Text renderer (set by UISystem during init).
    TextRenderer* textRenderer = nullptr;

private:
    float m_smoothedFps = 0.0f;
};

} // namespace Vestige
