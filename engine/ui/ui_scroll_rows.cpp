// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_scroll_rows.cpp
/// @brief UIScrollRows implementation.
#include "ui/ui_scroll_rows.h"

#include "ui/sprite_batch_renderer.h"
#include "ui/ui_theme.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

namespace
{

// Keeps a row count or row index from falling one short when a product of
// floats lands a hair under a whole number.
constexpr float kRowEpsilon = 1e-3f;

} // namespace

int UIScrollRows::rowOf(const UIElement& child) const
{
    if (child.position.y < viewTop)
    {
        return -1;
    }
    return static_cast<int>(std::floor((child.position.y - viewTop) / rowStep + kRowEpsilon));
}

int UIScrollRows::visibleRows() const
{
    return std::max(1, static_cast<int>(std::floor(viewHeight / rowStep + kRowEpsilon)));
}

bool UIScrollRows::rowShows(int row) const
{
    return row < 0 || (row >= m_firstRow && row < m_firstRow + visibleRows());
}

glm::vec2 UIScrollRows::scrollShift() const
{
    return {0.0f, -rowStep * static_cast<float>(m_firstRow)};
}

bool UIScrollRows::scrollRows(int rows)
{
    const int lastFirstRow = std::max(0, rowCount - visibleRows());
    const int target = std::clamp(m_firstRow + rows, 0, lastFirstRow);
    if (target == m_firstRow)
    {
        return false;
    }
    m_firstRow = target;
    return true;
}

bool UIScrollRows::reveal(const UIElement* child)
{
    const auto it = std::find_if(m_children.begin(), m_children.end(),
        [child](const std::unique_ptr<UIElement>& c) { return c.get() == child; });
    if (it == m_children.end())
    {
        return false;
    }
    const int row = rowOf(*child);
    if (row < 0 || rowShows(row))
    {
        return false;
    }
    m_firstRow = (row < m_firstRow) ? row : row - visibleRows() + 1;
    return true;
}

void UIScrollRows::render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                          int screenWidth, int screenHeight)
{
    if (!visible)
    {
        return;
    }

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    const glm::vec2 rowsPos = absPos + scrollShift();
    for (auto& child : m_children)
    {
        const int row = rowOf(*child);
        if (rowShows(row))
        {
            child->render(batch, row < 0 ? absPos : rowsPos, screenWidth, screenHeight);
        }
    }
    renderScrollBar(batch, absPos);
}

void UIScrollRows::renderScrollBar(SpriteBatchRenderer& batch, const glm::vec2& absPos) const
{
    const int shown = visibleRows();
    if (theme == nullptr || rowCount <= shown)
    {
        return;
    }
    const float width = theme->sliderTrackHeight * 2.0f;
    const glm::vec2 trackPos = absPos + glm::vec2(scrollBarX, viewTop);
    batch.drawQuad(trackPos, {width, viewHeight}, theme->rule);

    const float perRow = viewHeight / static_cast<float>(rowCount);
    batch.drawQuad(trackPos + glm::vec2(0.0f, perRow * static_cast<float>(m_firstRow)),
                   {width, perRow * static_cast<float>(shown)}, theme->accent);
}

bool UIScrollRows::hitTest(const glm::vec2& point, const glm::vec2& parentOffset,
                           int screenWidth, int screenHeight) const
{
    if (!visible)
    {
        return false;
    }

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    const glm::vec2 rowsPos = absPos + scrollShift();
    for (const auto& child : m_children)
    {
        const int row = rowOf(*child);
        if (rowShows(row)
            && child->hitTest(point, row < 0 ? absPos : rowsPos, screenWidth, screenHeight))
        {
            return true;
        }
    }
    return false;
}

UIElement* UIScrollRows::findInteractiveAt(const glm::vec2& point,
                                           const glm::vec2& parentOffset,
                                           int screenWidth, int screenHeight,
                                           glm::vec2& outAbsPos)
{
    if (!visible)
    {
        return nullptr;
    }

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    const glm::vec2 rowsPos = absPos + scrollShift();
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
    {
        const int row = rowOf(**it);
        if (!rowShows(row))
        {
            continue;
        }
        UIElement* hit = (*it)->findInteractiveAt(point, row < 0 ? absPos : rowsPos,
                                                  screenWidth, screenHeight, outAbsPos);
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

} // namespace Vestige
