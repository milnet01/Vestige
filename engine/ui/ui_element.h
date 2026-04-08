/// @file ui_element.h
/// @brief Base class for all in-game UI elements.
#pragma once

#include "ui/ui_signal.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

class SpriteBatchRenderer;

/// @brief Anchor positions for UI elements relative to their parent or screen.
enum class Anchor
{
    TOP_LEFT,
    TOP_CENTER,
    TOP_RIGHT,
    CENTER_LEFT,
    CENTER,
    CENTER_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_CENTER,
    BOTTOM_RIGHT
};

/// @brief Base class for UI elements (labels, images, panels).
///
/// Each element has a position (relative to its anchor), size, anchor point,
/// visibility, and interactivity. Elements can have children for hierarchy.
class UIElement
{
public:
    UIElement() = default;
    virtual ~UIElement() = default;

    // Non-copyable
    UIElement(const UIElement&) = delete;
    UIElement& operator=(const UIElement&) = delete;

    /// @brief Renders this element using the sprite batch.
    /// @param batch The sprite batch renderer.
    /// @param parentOffset Accumulated offset from parent elements.
    /// @param screenWidth Viewport width.
    /// @param screenHeight Viewport height.
    virtual void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                        int screenWidth, int screenHeight) = 0;

    /// @brief Tests if a screen-space point is within this element's bounds.
    /// @param point Screen-space point (pixels).
    /// @param parentOffset Accumulated offset from parent elements.
    /// @param screenWidth Viewport width.
    /// @param screenHeight Viewport height.
    /// @return True if the point hits this element.
    virtual bool hitTest(const glm::vec2& point, const glm::vec2& parentOffset,
                         int screenWidth, int screenHeight) const;

    /// @brief Computes the absolute screen position from anchor and offset.
    /// @param parentOffset Accumulated parent offset.
    /// @param screenWidth Viewport width.
    /// @param screenHeight Viewport height.
    /// @return Absolute screen position (top-left corner).
    glm::vec2 computeAbsolutePosition(const glm::vec2& parentOffset,
                                       int screenWidth, int screenHeight) const;

    /// @brief Adds a child element.
    /// @param child The child element (ownership transferred).
    void addChild(std::unique_ptr<UIElement> child);

    /// @brief Removes all children.
    void clearChildren();

    /// @brief Gets the number of children.
    size_t getChildCount() const { return m_children.size(); }

    // -- Properties --
    glm::vec2 position = {0.0f, 0.0f};  ///< Position relative to anchor
    glm::vec2 size = {100.0f, 30.0f};   ///< Width and height in pixels
    Anchor anchor = Anchor::TOP_LEFT;    ///< Anchor point on screen/parent
    bool visible = true;                 ///< Whether this element is rendered
    bool interactive = false;            ///< Whether this element receives input

    // -- Signals --
    Signal<> onClick;   ///< Fired when clicked
    Signal<> onHover;   ///< Fired when mouse enters

protected:
    std::vector<std::unique_ptr<UIElement>> m_children;
};

} // namespace Vestige
