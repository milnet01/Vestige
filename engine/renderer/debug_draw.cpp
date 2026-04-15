// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file debug_draw.cpp
/// @brief Immediate-mode debug line rendering implementation.
#include "renderer/debug_draw.h"
#include "core/logger.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

// Static vertex buffer shared by all static draw methods
std::vector<DebugDraw::DebugVertex> DebugDraw::s_vertices;

// Initial VBO capacity in vertices (grows if needed)
static constexpr size_t INITIAL_CAPACITY = 4096;

bool DebugDraw::initialize(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    // Load debug line shader
    std::string vertPath = assetPath + "/shaders/debug_line.vert.glsl";
    std::string fragPath = assetPath + "/shaders/debug_line.frag.glsl";
    if (!m_shader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("DebugDraw: failed to load debug_line shaders");
        return false;
    }

    // Create VBO with DSA (dynamic storage for per-frame streaming)
    m_vboCapacity = INITIAL_CAPACITY;
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo,
                         static_cast<GLsizeiptr>(m_vboCapacity * sizeof(DebugVertex)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);

    // Create VAO with DSA
    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(DebugVertex));

    // Position attribute (location 0)
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(DebugVertex, position));
    glVertexArrayAttribBinding(m_vao, 0, 0);

    // Color attribute (location 1)
    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(DebugVertex, color));
    glVertexArrayAttribBinding(m_vao, 1, 0);

    // Reserve space in the CPU-side vertex buffer
    s_vertices.reserve(INITIAL_CAPACITY);

    m_initialized = true;
    Logger::info("DebugDraw initialized");
    return true;
}

void DebugDraw::cleanup()
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
    m_shader.destroy();
    s_vertices.clear();
    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Static draw methods — queue line segments into s_vertices
// ---------------------------------------------------------------------------

void DebugDraw::line(const glm::vec3& from, const glm::vec3& to,
                     const glm::vec3& color)
{
    s_vertices.push_back({from, color});
    s_vertices.push_back({to,   color});
}

void DebugDraw::circle(const glm::vec3& center, const glm::vec3& normal,
                       float radius, const glm::vec3& color, int segments)
{
    // Build two perpendicular vectors in the circle's plane
    glm::vec3 n = glm::normalize(normal);
    glm::vec3 up = (std::abs(glm::dot(n, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                   ? glm::vec3(1.0f, 0.0f, 0.0f)
                   : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(n, up));
    glm::vec3 forward = glm::cross(right, n);

    float step = glm::two_pi<float>() / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i)
    {
        float a0 = step * static_cast<float>(i);
        float a1 = step * static_cast<float>(i + 1);
        glm::vec3 p0 = center + radius * (right * std::cos(a0) + forward * std::sin(a0));
        glm::vec3 p1 = center + radius * (right * std::cos(a1) + forward * std::sin(a1));
        s_vertices.push_back({p0, color});
        s_vertices.push_back({p1, color});
    }
}

void DebugDraw::wireSphere(const glm::vec3& center, float radius,
                           const glm::vec3& color, int segments)
{
    // Three orthogonal great circles
    circle(center, glm::vec3(0.0f, 0.0f, 1.0f), radius, color, segments); // XY
    circle(center, glm::vec3(0.0f, 1.0f, 0.0f), radius, color, segments); // XZ
    circle(center, glm::vec3(1.0f, 0.0f, 0.0f), radius, color, segments); // YZ
}

void DebugDraw::cone(const glm::vec3& apex, const glm::vec3& direction,
                     float length, float angleDeg,
                     const glm::vec3& color, int ribs)
{
    float angleRad = glm::radians(angleDeg);
    float endRadius = length * std::tan(angleRad);
    glm::vec3 dir = glm::normalize(direction);
    glm::vec3 endCenter = apex + dir * length;

    // Draw the far circle
    circle(endCenter, dir, endRadius, color, 32);

    // Build basis vectors for the far circle plane
    glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                   ? glm::vec3(1.0f, 0.0f, 0.0f)
                   : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(dir, up));
    glm::vec3 forward = glm::cross(right, dir);

    // Draw rib lines from apex to evenly spaced points on the far circle
    float step = glm::two_pi<float>() / static_cast<float>(ribs);
    for (int i = 0; i < ribs; ++i)
    {
        float angle = step * static_cast<float>(i);
        glm::vec3 rimPoint = endCenter
            + endRadius * (right * std::cos(angle) + forward * std::sin(angle));
        s_vertices.push_back({apex, color});
        s_vertices.push_back({rimPoint, color});
    }
}

void DebugDraw::arrow(const glm::vec3& from, const glm::vec3& to,
                      const glm::vec3& color, float headSize)
{
    // Main line
    s_vertices.push_back({from, color});
    s_vertices.push_back({to,   color});

    // Arrowhead: two lines from the tip, angled back
    glm::vec3 dir = to - from;
    float len = glm::length(dir);
    if (len < 0.001f)
    {
        return;
    }
    dir /= len;

    float headLen = len * headSize;

    // Build a perpendicular vector for the arrowhead wings
    glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                   ? glm::vec3(1.0f, 0.0f, 0.0f)
                   : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(dir, up));
    glm::vec3 forward = glm::cross(right, dir);

    glm::vec3 base = to - dir * headLen;
    float wingSpread = headLen * 0.4f;

    // Four wing lines (cross pattern)
    s_vertices.push_back({to, color});
    s_vertices.push_back({base + right * wingSpread, color});
    s_vertices.push_back({to, color});
    s_vertices.push_back({base - right * wingSpread, color});
    s_vertices.push_back({to, color});
    s_vertices.push_back({base + forward * wingSpread, color});
    s_vertices.push_back({to, color});
    s_vertices.push_back({base - forward * wingSpread, color});
}

size_t DebugDraw::getQueuedVertexCount()
{
    return s_vertices.size();
}

// ---------------------------------------------------------------------------
// flush — upload queued vertices and render
// ---------------------------------------------------------------------------

void DebugDraw::flush(const glm::mat4& viewProjection)
{
    if (!m_initialized || s_vertices.empty())
    {
        s_vertices.clear();
        return;
    }

    size_t vertexCount = s_vertices.size();

    // Upload vertex data to GPU (DSA)
    if (vertexCount > m_vboCapacity)
    {
        // Grow: delete old immutable buffer and create a larger one
        glDeleteBuffers(1, &m_vbo);
        m_vboCapacity = vertexCount * 2;
        glCreateBuffers(1, &m_vbo);
        glNamedBufferStorage(m_vbo,
                             static_cast<GLsizeiptr>(m_vboCapacity * sizeof(DebugVertex)),
                             nullptr, GL_DYNAMIC_STORAGE_BIT);
        // Re-bind new VBO to VAO binding point 0
        glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(DebugVertex));
    }
    glNamedBufferSubData(m_vbo, 0,
                         static_cast<GLsizeiptr>(vertexCount * sizeof(DebugVertex)),
                         s_vertices.data());

    // Save state
    GLboolean prevDepthMask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    // Render setup: depth test on, depth write off
    // Note: glLineWidth > 1.0 is unsupported on Mesa/AMD (GL_INVALID_VALUE).
    // Lines are always 1px wide; use geometry or screen-space techniques for thicker lines.
    glDepthMask(GL_FALSE);

    m_shader.use();
    m_shader.setMat4("u_viewProjection", viewProjection);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertexCount));
    glBindVertexArray(0);

    // Restore state
    glDepthMask(prevDepthMask);

    // Clear the buffer for the next frame
    s_vertices.clear();
}

} // namespace Vestige
