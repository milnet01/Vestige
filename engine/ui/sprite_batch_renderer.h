// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_batch_renderer.h
/// @brief Batched 2D quad rendering for UI elements.
#pragma once

#include "renderer/shader.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Vertex data for a single sprite batch vertex.
struct SpriteVertex
{
    glm::vec2 position;
    glm::vec2 texCoord;
    glm::vec4 color;
};

/// @brief Efficient batched 2D quad renderer for UI overlays.
///
/// Collects quads between begin() and end(), batching by texture to minimize
/// draw calls. Uses orthographic projection matching screen resolution.
/// Depth testing is disabled during rendering.
class SpriteBatchRenderer
{
public:
    /// @brief Maximum quads per batch (4 vertices per quad).
    static constexpr int MAX_QUADS = 1000;
    static constexpr int MAX_VERTICES = MAX_QUADS * 4;
    static constexpr int MAX_INDICES = MAX_QUADS * 6;

    SpriteBatchRenderer();
    ~SpriteBatchRenderer();

    // Non-copyable
    SpriteBatchRenderer(const SpriteBatchRenderer&) = delete;
    SpriteBatchRenderer& operator=(const SpriteBatchRenderer&) = delete;

    /// @brief Initializes GL resources (VAO, VBO, EBO, shader).
    /// @param assetPath Base path to shader assets.
    /// @return True if initialization succeeded.
    bool initialize(const std::string& assetPath);

    /// @brief Releases GL resources.
    void shutdown();

    /// @brief Begins a new batch frame with orthographic projection.
    /// @param screenWidth Viewport width in pixels.
    /// @param screenHeight Viewport height in pixels.
    void begin(int screenWidth, int screenHeight);

    /// @brief Queues a solid-color quad.
    /// @param position Top-left corner (screen pixels).
    /// @param size Width and height (pixels).
    /// @param color RGBA color.
    void drawQuad(const glm::vec2& position, const glm::vec2& size,
                  const glm::vec4& color);

    /// @brief Queues a textured quad.
    /// @param position Top-left corner (screen pixels).
    /// @param size Width and height (pixels).
    /// @param texture OpenGL texture ID.
    /// @param tint RGBA tint (multiplied with texture color).
    void drawTexturedQuad(const glm::vec2& position, const glm::vec2& size,
                          GLuint texture, const glm::vec4& tint = glm::vec4(1.0f));

    /// @brief Flushes all queued quads to the GPU.
    void end();

    /// @brief Whether the renderer is initialized.
    bool isInitialized() const { return m_initialized; }

private:
    /// @brief Flushes the current batch (same texture group).
    void flush();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    Shader m_shader;
    bool m_initialized = false;

    // Current batch state
    std::vector<SpriteVertex> m_vertices;
    GLuint m_currentTexture = 0;
    int m_quadCount = 0;
    glm::mat4 m_projection = glm::mat4(1.0f);
};

} // namespace Vestige
