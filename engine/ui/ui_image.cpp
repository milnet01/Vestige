// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_image.cpp
/// @brief UIImage implementation.
#include "ui/ui_image.h"
#include "ui/sprite_batch_renderer.h"

namespace Vestige
{

void UIImage::render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                     int screenWidth, int screenHeight)
{
    if (!visible || texture == 0)
    {
        return;
    }

    glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    batch.drawTexturedQuad(absPos, size, texture, tint);

    for (auto& child : m_children)
    {
        child->render(batch, absPos, screenWidth, screenHeight);
    }
}

} // namespace Vestige
