// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_label.cpp
/// @brief UILabel implementation.
#include "ui/ui_label.h"
#include "ui/sprite_batch_renderer.h"

namespace Vestige
{

void UILabel::render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                     int screenWidth, int screenHeight)
{
    if (!visible || text.empty() || !textRenderer)
    {
        return;
    }

    glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);

    // Delegate to the engine's TextRenderer for actual glyph rendering
    textRenderer->renderText2D(text, absPos.x, absPos.y, scale, color,
                                screenWidth, screenHeight);

    // Render children
    for (auto& child : m_children)
    {
        child->render(batch, absPos, screenWidth, screenHeight);
    }
}

} // namespace Vestige
