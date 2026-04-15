// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_panel.cpp
/// @brief UIPanel implementation.
#include "ui/ui_panel.h"
#include "ui/sprite_batch_renderer.h"

namespace Vestige
{

void UIPanel::render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                     int screenWidth, int screenHeight)
{
    if (!visible)
    {
        return;
    }

    glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    batch.drawQuad(absPos, size, backgroundColor);

    for (auto& child : m_children)
    {
        child->render(batch, absPos, screenWidth, screenHeight);
    }
}

} // namespace Vestige
