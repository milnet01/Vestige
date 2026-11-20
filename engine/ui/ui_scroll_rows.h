// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_scroll_rows.h
/// @brief A container that shows as many equal-height rows as fit and
///        scrolls the rest into view a whole row at a time.
#pragma once

#include "ui/ui_element.h"

namespace Vestige
{

struct UITheme;

/// @brief Holds rows of controls that may not all fit (3D_E-0874: the player
///        Settings pages at a large interface scale).
///
/// Children keep the positions they would have with every row showing. A
/// child whose top is at or below `viewTop` belongs to the row it starts in;
/// it is drawn and takes the mouse only while that row shows, moved up by the
/// rows scrolled past. A child above `viewTop` is no row: it always draws,
/// unmoved (a UIFrameHook).
///
/// Scrolling moves by whole rows, so no row is ever cut by the view's edge
/// and nothing needs clipping. Keyboard focus still walks every row:
/// `UISystem` calls `reveal` when focus lands on a child.
class UIScrollRows : public UIElement
{
public:
    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    bool hitTest(const glm::vec2& point, const glm::vec2& parentOffset,
                 int screenWidth, int screenHeight) const override;

    UIElement* findInteractiveAt(const glm::vec2& point, const glm::vec2& parentOffset,
                                 int screenWidth, int screenHeight,
                                 glm::vec2& outAbsPos) override;

    /// @brief Scrolls by @a rows (positive: later rows come into view), stopping
    ///        at either end. Returns true when the view moved.
    bool scrollRows(int rows);

    /// @brief Scrolls just far enough to show @a child's row. Returns true when
    ///        the view moved; false when @a child is not a row of this element.
    bool reveal(const UIElement* child);

    /// @brief How many rows fit in the view: at least one.
    int visibleRows() const;

    /// @brief The row at the top of the view.
    int firstRow() const { return m_firstRow; }

    float viewTop    = 0.0f;   ///< Top of row 0, in this element's coordinates.
    float viewHeight = 0.0f;   ///< Rows must end within this height.
    float rowStep    = 1.0f;   ///< Top of one row to the top of the next.
    int   rowCount   = 0;      ///< Rows the page has.

    /// Left edge of the scroll bar, drawn only while some row is out of view.
    float scrollBarX = 0.0f;
    const UITheme* theme = nullptr;  ///< Scroll bar colours and width; none: no bar.

private:
    /// @brief The row @a child starts in, or -1 for a child above `viewTop`.
    int rowOf(const UIElement& child) const;
    bool rowShows(int row) const;
    /// @brief What to add to a row child's parent offset while scrolled.
    glm::vec2 scrollShift() const;
    void renderScrollBar(SpriteBatchRenderer& batch, const glm::vec2& absPos) const;

    int m_firstRow = 0;
};

} // namespace Vestige
