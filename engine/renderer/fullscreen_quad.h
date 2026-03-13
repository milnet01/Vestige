/// @file fullscreen_quad.h
/// @brief Fullscreen quad for post-processing and screen-space effects.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief A screen-filling quad used to render textures to the display.
class FullscreenQuad
{
public:
    FullscreenQuad();
    ~FullscreenQuad();

    // Non-copyable
    FullscreenQuad(const FullscreenQuad&) = delete;
    FullscreenQuad& operator=(const FullscreenQuad&) = delete;

    // Movable
    FullscreenQuad(FullscreenQuad&& other) noexcept;
    FullscreenQuad& operator=(FullscreenQuad&& other) noexcept;

    /// @brief Draws the fullscreen quad (binds VAO and issues draw call).
    void draw() const;

private:
    void create();
    void cleanup();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
};

} // namespace Vestige
