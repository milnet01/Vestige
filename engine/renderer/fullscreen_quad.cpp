/// @file fullscreen_quad.cpp
/// @brief Fullscreen quad implementation.
#include "renderer/fullscreen_quad.h"

namespace Vestige
{

FullscreenQuad::FullscreenQuad()
{
    create();
}

FullscreenQuad::~FullscreenQuad()
{
    cleanup();
}

FullscreenQuad::FullscreenQuad(FullscreenQuad&& other) noexcept
    : m_vao(other.m_vao)
    , m_vbo(other.m_vbo)
{
    other.m_vao = 0;
    other.m_vbo = 0;
}

FullscreenQuad& FullscreenQuad::operator=(FullscreenQuad&& other) noexcept
{
    if (this != &other)
    {
        cleanup();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        other.m_vao = 0;
        other.m_vbo = 0;
    }
    return *this;
}

void FullscreenQuad::draw() const
{
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void FullscreenQuad::create()
{
    // Two triangles forming a screen-filling quad
    // Each vertex: position (x, y), texCoord (u, v)
    float vertices[] = {
        // First triangle
        -1.0f, -1.0f,  0.0f, 0.0f,   // bottom-left
         1.0f, -1.0f,  1.0f, 0.0f,   // bottom-right
         1.0f,  1.0f,  1.0f, 1.0f,   // top-right

        // Second triangle
        -1.0f, -1.0f,  0.0f, 0.0f,   // bottom-left
         1.0f,  1.0f,  1.0f, 1.0f,   // top-right
        -1.0f,  1.0f,  0.0f, 1.0f,   // top-left
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute (location 0): vec2
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                         static_cast<void*>(nullptr));

    // Texture coordinate attribute (location 1): vec2
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                         reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
}

void FullscreenQuad::cleanup()
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

} // namespace Vestige
