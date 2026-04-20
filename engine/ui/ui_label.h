// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_label.h
/// @brief UI text label element.
#pragma once

#include "ui/ui_element.h"
#include "renderer/text_renderer.h"

namespace Vestige
{

/// @brief Displays text in screen-space using the engine's TextRenderer.
class UILabel : public UIElement
{
public:
    UILabel()
    {
        interactive = false;
        m_accessible.role = UIAccessibleRole::Label;
    }

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief The text to display.
    std::string text;

    /// @brief Text color (RGB, 0-1).
    glm::vec3 color = {1.0f, 1.0f, 1.0f};

    /// @brief Text scale factor (1.0 = font pixel size).
    float scale = 1.0f;

    /// @brief The text renderer to use (set by UISystem during init).
    TextRenderer* textRenderer = nullptr;
};

} // namespace Vestige
