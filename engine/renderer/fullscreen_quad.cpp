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

    // Create buffer with DSA (immutable storage for static geometry)
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, sizeof(vertices), vertices, 0);

    // Create VAO with DSA
    glCreateVertexArrays(1, &m_vao);

    // Bind VBO to binding point 0 (stride = 4 floats per vertex)
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, 4 * sizeof(float));

    // Position attribute (location 0): vec2
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_vao, 0, 0);

    // Texture coordinate attribute (location 1): vec2
    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
    glVertexArrayAttribBinding(m_vao, 1, 0);
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
