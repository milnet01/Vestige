// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_canvas.cpp
/// @brief UICanvas implementation.
#include "ui/ui_canvas.h"
#include "ui/sprite_batch_renderer.h"

namespace Vestige
{

void UICanvas::addElement(std::unique_ptr<UIElement> element)
{
    m_elements.push_back(std::move(element));
}

void UICanvas::clear()
{
    m_elements.clear();
}

void UICanvas::render(SpriteBatchRenderer& batch, int screenWidth, int screenHeight)
{
    glm::vec2 rootOffset(0.0f);
    for (auto& elem : m_elements)
    {
        if (elem->visible)
        {
            elem->render(batch, rootOffset, screenWidth, screenHeight);
        }
    }
}

bool UICanvas::hitTest(const glm::vec2& point, int screenWidth, int screenHeight) const
{
    glm::vec2 rootOffset(0.0f);
    for (const auto& elem : m_elements)
    {
        if (elem->hitTest(point, rootOffset, screenWidth, screenHeight))
        {
            return true;
        }
    }
    return false;
}

} // namespace Vestige
