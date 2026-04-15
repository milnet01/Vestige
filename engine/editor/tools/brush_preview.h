// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file brush_preview.h
/// @brief Renders a circle decal at the brush position for visual feedback.
#pragma once

#include "renderer/shader.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

namespace Vestige
{

/// @brief Renders a brush preview circle on the ground surface.
class BrushPreviewRenderer
{
public:
    BrushPreviewRenderer() = default;
    ~BrushPreviewRenderer();

    // Non-copyable
    BrushPreviewRenderer(const BrushPreviewRenderer&) = delete;
    BrushPreviewRenderer& operator=(const BrushPreviewRenderer&) = delete;

    /// @brief Initializes shaders and geometry.
    /// @param assetPath Path to the assets directory.
    /// @return True if initialization succeeded.
    bool init(const std::string& assetPath);

    /// @brief Releases GPU resources.
    void shutdown();

    /// @brief Renders a circle at the given world position.
    /// @param center World-space center of the brush.
    /// @param normal Surface normal at the brush position.
    /// @param radius Brush radius.
    /// @param viewProjection VP matrix.
    /// @param isEraser True = red circle, false = green circle.
    void render(const glm::vec3& center, const glm::vec3& normal, float radius,
                const glm::mat4& viewProjection, bool isEraser);

private:
    void createCircleGeometry();

    Shader m_shader;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    int m_vertexCount = 0;
    bool m_initialized = false;
};

} // namespace Vestige
