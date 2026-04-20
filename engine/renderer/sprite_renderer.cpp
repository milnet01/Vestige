// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_renderer.cpp
/// @brief SpriteRenderer implementation.
#include "renderer/sprite_renderer.h"
#include "renderer/sprite_atlas.h"
#include "core/logger.h"

namespace Vestige
{

namespace
{
constexpr std::size_t kInitialInstanceCapacity = 1024;
}

SpriteRenderer::SpriteRenderer() = default;

SpriteRenderer::~SpriteRenderer()
{
    if (m_initialized)
    {
        shutdown();
    }
}

bool SpriteRenderer::initialize(const std::string& assetPath)
{
    const std::string vertPath = assetPath + "/shaders/sprite.vert.glsl";
    const std::string fragPath = assetPath + "/shaders/sprite.frag.glsl";
    if (!m_shader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("[SpriteRenderer] Failed to load sprite shaders");
        return false;
    }

    // Corner quad — 4 vertices, drawn as a triangle fan. UV corners double
    // as normalised frame coords so the vertex shader can do
    // `mix(uvRect.xy, uvRect.zw, aCornerUv)` to compute the atlas UV.
    constexpr float kCorners[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_cornerVbo);
    glGenBuffers(1, &m_instanceVbo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_cornerVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCorners), kCorners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    m_instanceCapacity = kInitialInstanceCapacity;
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_instanceCapacity * sizeof(SpriteInstance)),
                 nullptr, GL_DYNAMIC_DRAW);

    // Locations 1..5 are per-instance. Divisor 1 → advance once per
    // instance rather than once per vertex.
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                          reinterpret_cast<void*>(offsetof(SpriteInstance, transformRow0)));
    glVertexAttribDivisor(1, 1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                          reinterpret_cast<void*>(offsetof(SpriteInstance, transformRow1)));
    glVertexAttribDivisor(2, 1);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                          reinterpret_cast<void*>(offsetof(SpriteInstance, uvRect)));
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                          reinterpret_cast<void*>(offsetof(SpriteInstance, tint)));
    glVertexAttribDivisor(4, 1);

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                          reinterpret_cast<void*>(offsetof(SpriteInstance, depth)));
    glVertexAttribDivisor(5, 1);

    glBindVertexArray(0);

    m_initialized = true;
    Logger::info("[SpriteRenderer] Initialized (capacity " +
                 std::to_string(m_instanceCapacity) + ")");
    return true;
}

void SpriteRenderer::shutdown()
{
    if (m_instanceVbo != 0) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_cornerVbo   != 0) { glDeleteBuffers(1, &m_cornerVbo);   m_cornerVbo   = 0; }
    if (m_vao         != 0) { glDeleteVertexArrays(1, &m_vao);    m_vao         = 0; }
    m_shader.destroy();
    m_instanceCapacity = 0;
    m_initialized = false;
}

std::size_t SpriteRenderer::ensureInstanceCapacity(std::size_t count)
{
    if (count <= m_instanceCapacity)
    {
        return m_instanceCapacity;
    }
    // Double until we fit, so repeated growth pays amortised O(1) per instance.
    std::size_t newCap = m_instanceCapacity > 0 ? m_instanceCapacity : kInitialInstanceCapacity;
    while (newCap < count) { newCap *= 2; }
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(newCap * sizeof(SpriteInstance)),
                 nullptr, GL_DYNAMIC_DRAW);
    m_instanceCapacity = newCap;
    return m_instanceCapacity;
}

void SpriteRenderer::begin(const glm::mat4& viewProj)
{
    if (!m_initialized)
    {
        return;
    }
    m_viewProj = viewProj;
    m_frameActive = true;

    // Cache GL state so end() can restore it — the renderer runs between
    // the 3D post-process and the UI overlay, both of which have their own
    // blend / depth assumptions.
    glGetBooleanv(GL_DEPTH_WRITEMASK, &m_savedDepthMask);
    m_savedBlend = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_savedBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &m_savedBlendDst);
}

void SpriteRenderer::drawBatch(const SpriteAtlas& atlas,
                               const std::vector<SpriteInstance>& instances,
                               bool isTransparent)
{
    if (!m_initialized || !m_frameActive || instances.empty())
    {
        return;
    }

    // State for this pass.
    if (isTransparent)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    else
    {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    m_shader.use();
    m_shader.setMat4("u_viewProj", m_viewProj);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.textureId());
    m_shader.setInt("u_atlas", 0);

    glBindVertexArray(m_vao);

    // Split the batch across multiple draws if it exceeds capacity — this
    // prevents a runaway caller from triggering a multi-GB VBO resize.
    std::size_t drawn = 0;
    while (drawn < instances.size())
    {
        const std::size_t chunk = std::min(instances.size() - drawn,
                                           MAX_INSTANCES_PER_DRAW);
        ensureInstanceCapacity(chunk);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(chunk * sizeof(SpriteInstance)),
                        instances.data() + drawn);
        glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, static_cast<GLsizei>(chunk));
        drawn += chunk;
    }

    glBindVertexArray(0);
}

void SpriteRenderer::end()
{
    if (!m_frameActive)
    {
        return;
    }
    m_frameActive = false;

    // Restore cached state so subsequent passes are not surprised.
    glDepthMask(m_savedDepthMask);
    if (m_savedBlend)
    {
        glEnable(GL_BLEND);
        glBlendFunc(static_cast<GLenum>(m_savedBlendSrc),
                    static_cast<GLenum>(m_savedBlendDst));
    }
    else
    {
        glDisable(GL_BLEND);
    }
}

} // namespace Vestige
