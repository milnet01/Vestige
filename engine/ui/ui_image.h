// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_image.h
/// @brief UI textured image element.
#pragma once

#include "ui/ui_element.h"

#include <glad/gl.h>

namespace Vestige
{

/// @brief Displays a textured quad in the UI.
class UIImage : public UIElement
{
public:
    UIImage() = default;

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief OpenGL texture ID to display.
    GLuint texture = 0;

    /// @brief Tint color (multiplied with texture). Alpha for transparency.
    glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace Vestige
