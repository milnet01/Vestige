// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_element.cpp
/// @brief UIElement base class implementation.
#include "ui/ui_element.h"

namespace Vestige
{

glm::vec2 UIElement::computeAbsolutePosition(const glm::vec2& parentOffset,
                                              int screenWidth, int screenHeight) const
{
    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    glm::vec2 anchorPos(0.0f);

    switch (anchor)
    {
        case Anchor::TOP_LEFT:      anchorPos = {0.0f, 0.0f}; break;
        case Anchor::TOP_CENTER:    anchorPos = {sw * 0.5f, 0.0f}; break;
        case Anchor::TOP_RIGHT:     anchorPos = {sw, 0.0f}; break;
        case Anchor::CENTER_LEFT:   anchorPos = {0.0f, sh * 0.5f}; break;
        case Anchor::CENTER:        anchorPos = {sw * 0.5f, sh * 0.5f}; break;
        case Anchor::CENTER_RIGHT:  anchorPos = {sw, sh * 0.5f}; break;
        case Anchor::BOTTOM_LEFT:   anchorPos = {0.0f, sh}; break;
        case Anchor::BOTTOM_CENTER: anchorPos = {sw * 0.5f, sh}; break;
        case Anchor::BOTTOM_RIGHT:  anchorPos = {sw, sh}; break;
    }

    return parentOffset + anchorPos + position;
}

bool UIElement::hitTest(const glm::vec2& point, const glm::vec2& parentOffset,
                         int screenWidth, int screenHeight) const
{
    // Phase 10.9 Slice 3 S3 — hidden subtrees are skipped wholesale:
    // a child must not catch input that the sighted user cannot see
    // a parent for. This matches collectAccessible's subtree-skip
    // policy in the same file.
    if (!visible)
    {
        return false;
    }

    const glm::vec2 absPos =
        computeAbsolutePosition(parentOffset, screenWidth, screenHeight);

    // Children are checked first: they render on top of the parent,
    // so logical z-order dictates they catch the hit before the
    // parent's own bounds do. Also closes Slice 3 S5 (UIPanel
    // delegation) as a side effect — a non-interactive container
    // still lets its interactive children catch input.
    for (const auto& child : m_children)
    {
        if (child && child->hitTest(point, absPos, screenWidth, screenHeight))
        {
            return true;
        }
    }

    // Self-hit requires `interactive`. A non-interactive container
    // whose children did not catch the hit returns false — the hit
    // passes through, which is the expected dispatch behaviour for
    // a purely decorative panel.
    if (!interactive)
    {
        return false;
    }

    return point.x >= absPos.x && point.x <= absPos.x + size.x &&
           point.y >= absPos.y && point.y <= absPos.y + size.y;
}

void UIElement::addChild(std::unique_ptr<UIElement> child)
{
    m_children.push_back(std::move(child));
}

void UIElement::clearChildren()
{
    m_children.clear();
}

void UIElement::collectAccessible(std::vector<UIAccessibilitySnapshot>& out) const
{
    // Hidden subtrees are entirely skipped — a screen reader should
    // not announce UI the sighted user cannot see either.
    if (!visible)
    {
        return;
    }

    // Elements with neither a role nor a label are purely decorative
    // spacers / containers: include nothing for self, but still
    // recurse so labelled descendants are discovered.
    const bool hasRole  = m_accessible.role != UIAccessibleRole::Unknown;
    const bool hasLabel = !m_accessible.label.empty();
    if (hasRole || hasLabel)
    {
        UIAccessibilitySnapshot snap;
        snap.info        = m_accessible;
        snap.interactive = interactive;
        out.push_back(std::move(snap));
    }

    for (const auto& child : m_children)
    {
        if (child)
        {
            child->collectAccessible(out);
        }
    }
}

} // namespace Vestige
