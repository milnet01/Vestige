/// @file sprite_batch_renderer.cpp
/// @brief SpriteBatchRenderer implementation.
#include "ui/sprite_batch_renderer.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Vestige
{

SpriteBatchRenderer::SpriteBatchRenderer()
{
    m_vertices.reserve(MAX_VERTICES);
}

SpriteBatchRenderer::~SpriteBatchRenderer()
{
    if (m_initialized)
    {
        shutdown();
    }
}

bool SpriteBatchRenderer::initialize(const std::string& assetPath)
{
    // Load UI sprite shader
    std::string vertPath = assetPath + "/shaders/ui_sprite.vert.glsl";
    std::string fragPath = assetPath + "/shaders/ui_sprite.frag.glsl";
    if (!m_shader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("[SpriteBatchRenderer] Failed to load UI sprite shader");
        return false;
    }

    // Generate index buffer (shared pattern for all quads: 0,1,2, 2,3,0)
    std::vector<uint32_t> indices(MAX_INDICES);
    for (int i = 0; i < MAX_QUADS; ++i)
    {
        uint32_t base = static_cast<uint32_t>(i * 4);
        int idx = i * 6;
        indices[static_cast<size_t>(idx + 0)] = base + 0;
        indices[static_cast<size_t>(idx + 1)] = base + 1;
        indices[static_cast<size_t>(idx + 2)] = base + 2;
        indices[static_cast<size_t>(idx + 3)] = base + 2;
        indices[static_cast<size_t>(idx + 4)] = base + 3;
        indices[static_cast<size_t>(idx + 5)] = base + 0;
    }

    // Create VAO, VBO, EBO
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    // VBO — dynamic, filled each frame
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(MAX_VERTICES * sizeof(SpriteVertex)),
                 nullptr, GL_DYNAMIC_DRAW);

    // EBO — static index pattern
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    // Vertex attributes
    // Position (vec2) at location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<void*>(offsetof(SpriteVertex, position)));

    // TexCoord (vec2) at location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<void*>(offsetof(SpriteVertex, texCoord)));

    // Color (vec4) at location 2
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<void*>(offsetof(SpriteVertex, color)));

    glBindVertexArray(0);

    m_initialized = true;
    Logger::info("[SpriteBatchRenderer] Initialized");
    return true;
}

void SpriteBatchRenderer::shutdown()
{
    if (m_ebo != 0) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_initialized = false;
    Logger::info("[SpriteBatchRenderer] Shut down");
}

void SpriteBatchRenderer::begin(int screenWidth, int screenHeight)
{
    m_vertices.clear();
    m_quadCount = 0;
    m_currentTexture = 0;

    // Orthographic projection: (0,0) top-left, (w,h) bottom-right
    m_projection = glm::ortho(0.0f, static_cast<float>(screenWidth),
                               static_cast<float>(screenHeight), 0.0f,
                               -1.0f, 1.0f);
}

void SpriteBatchRenderer::drawQuad(const glm::vec2& position, const glm::vec2& size,
                                    const glm::vec4& color)
{
    // Flush if switching from textured to untextured or batch is full
    if (m_currentTexture != 0 || m_quadCount >= MAX_QUADS)
    {
        flush();
        m_currentTexture = 0;
    }

    float x = position.x;
    float y = position.y;
    float w = size.x;
    float h = size.y;

    // Four vertices: top-left, top-right, bottom-right, bottom-left
    m_vertices.push_back({{x,     y},     {0.0f, 0.0f}, color});
    m_vertices.push_back({{x + w, y},     {1.0f, 0.0f}, color});
    m_vertices.push_back({{x + w, y + h}, {1.0f, 1.0f}, color});
    m_vertices.push_back({{x,     y + h}, {0.0f, 1.0f}, color});
    ++m_quadCount;
}

void SpriteBatchRenderer::drawTexturedQuad(const glm::vec2& position,
                                            const glm::vec2& size,
                                            GLuint texture,
                                            const glm::vec4& tint)
{
    // Flush if texture changed or batch is full
    if (m_currentTexture != texture || m_quadCount >= MAX_QUADS)
    {
        flush();
        m_currentTexture = texture;
    }

    float x = position.x;
    float y = position.y;
    float w = size.x;
    float h = size.y;

    m_vertices.push_back({{x,     y},     {0.0f, 0.0f}, tint});
    m_vertices.push_back({{x + w, y},     {1.0f, 0.0f}, tint});
    m_vertices.push_back({{x + w, y + h}, {1.0f, 1.0f}, tint});
    m_vertices.push_back({{x,     y + h}, {0.0f, 1.0f}, tint});
    ++m_quadCount;
}

void SpriteBatchRenderer::end()
{
    flush();
}

void SpriteBatchRenderer::flush()
{
    if (m_quadCount == 0)
    {
        return;
    }

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(m_vertices.size() * sizeof(SpriteVertex)),
                    m_vertices.data());

    // Set up render state
    m_shader.use();
    m_shader.setMat4("u_projection", m_projection);
    m_shader.setInt("u_hasTexture", m_currentTexture != 0 ? 1 : 0);

    if (m_currentTexture != 0)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_currentTexture);
        m_shader.setInt("u_texture", 0);
    }

    // Draw
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_quadCount * 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Reset batch
    m_vertices.clear();
    m_quadCount = 0;
}

} // namespace Vestige
