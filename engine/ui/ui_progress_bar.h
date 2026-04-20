// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_progress_bar.h
/// @brief Horizontal progress bar HUD widget (health / stamina / load progress).
#pragma once

#include "ui/ui_element.h"

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief A horizontal bar that fills left-to-right by `value / maxValue`.
///
/// Renders as a background rect (`emptyColor`) overlaid by a fill rect
/// (`fillColor`) sized to the current ratio. Position / size / anchor follow
/// the standard UIElement contract. `value` is clamped to `[0, maxValue]`
/// at render time.
class UIProgressBar : public UIElement
{
public:
    UIProgressBar()
    {
        interactive = false;
        size = {120.0f, 18.0f};
        m_accessible.role = UIAccessibleRole::ProgressBar;
    }

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief Current value (clamped to [0, maxValue] at render time).
    float value = 1.0f;

    /// @brief Maximum value. Must be > 0 for a visible fill.
    float maxValue = 1.0f;

    /// @brief Foreground (fill) colour.
    glm::vec4 fillColor  = {0.20f, 0.85f, 0.30f, 1.0f};

    /// @brief Background (empty) colour.
    glm::vec4 emptyColor = {0.10f, 0.10f, 0.12f, 0.7f};

    /// @brief Returns the clamped fill ratio in [0, 1].
    float getRatio() const;
};

} // namespace Vestige
