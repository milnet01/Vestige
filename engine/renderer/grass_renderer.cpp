// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_renderer.cpp
/// @brief GPU procedural grass renderer — G1 skeleton (design
///        docs/phases/phase_10_meadow_gpu_grass_design.md §5.1/§5.6).

#include "renderer/grass_renderer.h"

#include "core/logger.h"

#include <glm/gtc/constants.hpp>

namespace Vestige
{

GrassRenderer::~GrassRenderer()
{
    shutdown();
}

bool GrassRenderer::init(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    if (!m_shader.loadFromFiles(assetPath + "/shaders/grass.vert.glsl",
                                assetPath + "/shaders/grass.frag.glsl"))
    {
        Logger::error("GrassRenderer: failed to load grass shaders");
        return false;
    }

    // An attribute-less draw (geometry from gl_VertexID/gl_InstanceID + SSBO) still needs
    // a bound non-zero VAO — glDrawArrays* with VAO 0 is GL_INVALID_OPERATION in a core
    // profile (design §5.1; mirrors particle_renderer's m_gpuVao).
    glCreateVertexArrays(1, &m_vao);

    m_initialized = true;
    Logger::info("GrassRenderer initialized (G1)");
    return true;
}

void GrassRenderer::seatTestPatchAt(const glm::vec3& center)
{
    if (!m_initialized)
    {
        return;
    }

    // A small deterministic jittered grid of blades around `center` — enough to exercise
    // the VS generator + SSBO draw. No RNG: a cheap spatial hash gives per-blade variety
    // so a formula bug can't hide behind uniform blades. G2 replaces this with real
    // terrain/slope/splat-gated scatter.
    constexpr int SIDE = 48;             // 48×48 = 2304 blades
    constexpr float SPACING = 0.11f;     // metres between grid cells

    std::vector<GrassBlade> blades;
    blades.reserve(static_cast<size_t>(SIDE) * SIDE);

    for (int z = 0; z < SIDE; ++z)
    {
        for (int x = 0; x < SIDE; ++x)
        {
            const uint32_t h = static_cast<uint32_t>(x) * 73856093u
                             ^ static_cast<uint32_t>(z) * 19349663u;
            const float j0 = static_cast<float>(h & 0xFFFFu) / 65535.0f;         // 0..1
            const float j1 = static_cast<float>((h >> 16) & 0xFFFFu) / 65535.0f; // 0..1

            constexpr float HALF = static_cast<float>(SIDE) * 0.5f;
            GrassBlade b;
            b.rootPos = center + glm::vec3(
                (static_cast<float>(x) - HALF + (j0 - 0.5f)) * SPACING,
                0.0f,
                (static_cast<float>(z) - HALF + (j1 - 0.5f)) * SPACING);
            b.height = 0.45f + 0.40f * j0;
            b.facingAngle = j1 * glm::two_pi<float>();
            b.lean = 0.15f + 0.25f * j0;
            b.width = 0.03f + 0.02f * j1;
            b.hash = h;
            blades.push_back(b);
        }
    }

    uploadBlades(blades);
    Logger::info("GrassRenderer: seated G1 test patch (" +
                 std::to_string(m_bladeCount) + " blades)");
}

void GrassRenderer::uploadBlades(const std::vector<GrassBlade>& blades)
{
    m_bladeCount = static_cast<GLsizei>(blades.size());
    if (m_bladeCount == 0)
    {
        return;
    }

    if (m_bladeSSBO == 0)
    {
        glCreateBuffers(1, &m_bladeSSBO);
    }
    // Mutable storage (glNamedBufferData): the field is uploaded once here but G2 rebuilds
    // it when the terrain changes, so the buffer must stay reallocatable.
    glNamedBufferData(m_bladeSSBO,
                      static_cast<GLsizeiptr>(blades.size() * sizeof(GrassBlade)),
                      blades.data(), GL_STATIC_DRAW);
}

void GrassRenderer::render(const glm::mat4& viewProjection)
{
    if (!m_initialized || m_bladeCount == 0 || m_shader.getId() == 0)
    {
        return;
    }

    // Opaque, two-sided blades (design §5.1): depth-tested + written, blend off,
    // back-face cull off. Set explicitly so prior passes' state can't leak in.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    m_shader.use();
    m_shader.setMat4("u_viewProjection", viewProjection);
    m_shader.setInt("u_segments", SEGMENTS);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_bladeSSBO);
    glBindVertexArray(m_vao);

    // One instanced strip per blade; strips do not connect across instances (design §5.1).
    const GLsizei vertsPerBlade = 2 * SEGMENTS + 1;
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, vertsPerBlade, m_bladeCount);

    glBindVertexArray(0);
}

void GrassRenderer::shutdown()
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_bladeSSBO != 0)
    {
        glDeleteBuffers(1, &m_bladeSSBO);
        m_bladeSSBO = 0;
    }
    m_shader.destroy();
    m_bladeCount = 0;
    m_initialized = false;
}

} // namespace Vestige
