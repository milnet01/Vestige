// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_crosshair.h
/// @brief Centered cross HUD widget for first-person camera modes.
#pragma once

#include "ui/ui_element.h"

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Renders a thin cross at the screen centre (regardless of position/anchor).
///
/// Useful for FPS-style first-person camera modes where the user needs an
/// aiming reticle. Configurable arm length, thickness, gap (centre dot
/// radius — set to 0 for a solid plus, > 0 for a "T" gap pattern).
class UICrosshair : public UIElement
{
public:
    UICrosshair()
    {
        interactive = false;
        m_accessible.role = UIAccessibleRole::Crosshair;
    }

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief Length of each crosshair arm, in pixels.
    float armLength = 12.0f;

    /// @brief Thickness of each arm, in pixels.
    float thickness = 2.0f;

    /// @brief Pixel gap between centre and arm start (0 = solid plus).
    float centreGap = 4.0f;

    /// @brief Crosshair colour (RGBA).
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 0.85f};
};

} // namespace Vestige
