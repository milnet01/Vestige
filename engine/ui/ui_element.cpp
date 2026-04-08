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
    if (!visible || !interactive)
    {
        return false;
    }

    glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
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

} // namespace Vestige
