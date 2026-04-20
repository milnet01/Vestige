// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_world_label.h
/// @brief In-world floating text — projects a 3D world position to a 2D screen label.
#pragma once

#include "ui/ui_element.h"

#include <glm/glm.hpp>
#include <string>

namespace Vestige
{

class Camera;
class TextRenderer;

/// @brief Renders text anchored to a world-space position (billboarded screen text).
///
/// Each frame, projects `worldPosition` through the camera's view + projection
/// matrices and draws the text at the resulting screen pixel via
/// `TextRenderer::renderText2D`. Off-screen / behind-camera labels are
/// silently skipped (frustum culled by `projectWorldToScreen`).
///
/// The base UIElement's `position` / `anchor` fields are intentionally
/// ignored — world-space anchoring takes precedence.
class UIWorldLabel : public UIElement
{
public:
    UIWorldLabel() { interactive = false; }

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief World-space position the text follows.
    glm::vec3 worldPosition = {0.0f, 0.0f, 0.0f};

    /// @brief Pixel offset added after projection (e.g. lift text above an entity head).
    glm::vec2 screenOffset = {0.0f, -24.0f};

    /// @brief Text content.
    std::string text;

    /// @brief Text colour.
    glm::vec3 color = {1.0f, 1.0f, 1.0f};

    /// @brief Text scale factor (1.0 = TextRenderer's pixel size).
    float scale = 0.5f;

    /// @brief Camera used for projection (must be set before render).
    Camera* camera = nullptr;

    /// @brief Text renderer (set by UISystem).
    TextRenderer* textRenderer = nullptr;
};

} // namespace Vestige
