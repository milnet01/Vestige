// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_panel.h
/// @brief UI background panel element.
#pragma once

#include "ui/ui_element.h"

namespace Vestige
{

/// @brief A solid-color or textured background rectangle.
class UIPanel : public UIElement
{
public:
    UIPanel() = default;

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief Background color (RGBA).
    glm::vec4 backgroundColor = {0.0f, 0.0f, 0.0f, 0.5f};
};

} // namespace Vestige
