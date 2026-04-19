// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file brush_preview.cpp
/// @brief BrushPreviewRenderer implementation — circle decal overlay.
#include "editor/tools/brush_preview.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace Vestige
{

BrushPreviewRenderer::~BrushPreviewRenderer()
{
    shutdown();
}

bool BrushPreviewRenderer::init(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    if (!m_shader.loadFromFiles(assetPath + "/shaders/brush_preview.vert.glsl",
                                assetPath + "/shaders/brush_preview.frag.glsl"))
    {
        Logger::error("Failed to load brush preview shaders");
        return false;
    }

    createCircleGeometry();

    m_initialized = true;
    return true;
}

void BrushPreviewRenderer::shutdown()
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
    m_initialized = false;
}

void BrushPreviewRenderer::render(const glm::vec3& center, const glm::vec3& normal,
                                   float radius, const glm::mat4& viewProjection,
                                   bool isEraser)
{
    if (!m_initialized)
    {
        return;
    }

    // Build model matrix: translate to center, scale to radius, orient to surface
    // For now, assume ground-plane (y-up) orientation
    (void)normal;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::scale(model, glm::vec3(radius));

    glm::vec3 color = isEraser ? glm::vec3(0.9f, 0.2f, 0.2f) : glm::vec3(0.2f, 0.9f, 0.3f);

    m_shader.use();
    m_shader.setMat4("u_mvp", viewProjection * model);
    m_shader.setVec3("u_color", color);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_LINE_LOOP, 0, m_vertexCount);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void BrushPreviewRenderer::createCircleGeometry()
{
    // Circle outline on the XZ plane (y=0.01 slight offset above ground)
    const int segments = 64;
    std::vector<glm::vec3> vertices;
    vertices.reserve(segments);

    for (int i = 0; i < segments; ++i)
    {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        vertices.push_back(glm::vec3(std::cos(angle), 0.01f, std::sin(angle)));
    }

    m_vertexCount = segments;

    glCreateVertexArrays(1, &m_vao);
    glCreateBuffers(1, &m_vbo);

    // Upload data (immutable, static)
    glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3)),
                         vertices.data(), 0);

    // Bind VBO to VAO binding point 0
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(glm::vec3));

    // Attribute 0: vec3 position at binding 0
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_vao, 0, 0);
}

} // namespace Vestige
