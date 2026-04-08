/// @file ui_canvas.h
/// @brief Screen-space UI canvas that manages an element hierarchy.
#pragma once

#include "ui/ui_element.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Vestige
{

class SpriteBatchRenderer;

/// @brief Root container for screen-space UI elements.
///
/// Owns a flat list of root elements. Handles render traversal and hit testing.
class UICanvas
{
public:
    UICanvas() = default;

    /// @brief Adds a root element to the canvas.
    /// @param element The element (ownership transferred).
    void addElement(std::unique_ptr<UIElement> element);

    /// @brief Removes all elements.
    void clear();

    /// @brief Gets the number of root elements.
    size_t getElementCount() const { return m_elements.size(); }

    /// @brief Renders all visible elements.
    /// @param batch The sprite batch renderer.
    /// @param screenWidth Viewport width.
    /// @param screenHeight Viewport height.
    void render(SpriteBatchRenderer& batch, int screenWidth, int screenHeight);

    /// @brief Tests if any interactive element is under the given point.
    /// @param point Screen-space point (pixels).
    /// @param screenWidth Viewport width.
    /// @param screenHeight Viewport height.
    /// @return True if an interactive element is hit.
    bool hitTest(const glm::vec2& point, int screenWidth, int screenHeight) const;

private:
    std::vector<std::unique_ptr<UIElement>> m_elements;
};

} // namespace Vestige
