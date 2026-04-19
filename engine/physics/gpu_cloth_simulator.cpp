// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_cloth_simulator.cpp
/// @brief GpuClothSimulator skeleton — Phase 9B Step 2.

#include "physics/gpu_cloth_simulator.h"
#include "core/logger.h"

#include <GLFW/glfw3.h>

namespace Vestige
{

GpuClothSimulator::GpuClothSimulator() = default;

GpuClothSimulator::~GpuClothSimulator()
{
    destroyBuffers();
}

bool GpuClothSimulator::isSupported()
{
    // Probe is safe to call without a context. GLAD reports zero / nulls
    // for the version query when no context is current; treat that as
    // "not supported" rather than crashing on the GL call.
    if (glfwGetCurrentContext() == nullptr) return false;

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    // Compute shaders + SSBO are core in 4.3. We require 4.5 because the
    // engine uses glCreateBuffers / glNamedBufferStorage (DSA, 4.5 core).
    return (major > 4) || (major == 4 && minor >= 5);
}

void GpuClothSimulator::initialize(const ClothConfig& config, uint32_t /*seed*/)
{
    if (m_initialized) destroyBuffers();

    m_gridW = config.width;
    m_gridH = config.height;
    m_particleCount = m_gridW * m_gridH;

    if (m_particleCount == 0)
    {
        Logger::warning("[GpuClothSimulator] Refusing to initialize with zero particles");
        return;
    }

    buildInitialGrid(config);
    createBuffers();

    m_initialized = true;
    Logger::info("[GpuClothSimulator] Initialized "
                 + std::to_string(m_gridW) + "x" + std::to_string(m_gridH)
                 + " grid (" + std::to_string(m_particleCount) + " particles)");
}

void GpuClothSimulator::simulate(float /*deltaTime*/)
{
    // Step 2: no-op. Force/integrate/constraint compute dispatches land in
    // Steps 3–5. Particles do not move.
}

void GpuClothSimulator::reset()
{
    if (!m_initialized) return;
    // No simulation has occurred (Step 2 simulate is a no-op), so the GPU
    // state already matches the initial grid. Nothing to do until later
    // steps introduce mutable particle state on the GPU.
}

const glm::vec3* GpuClothSimulator::getPositions() const
{
    return m_positionMirror.empty() ? nullptr : m_positionMirror.data();
}

const glm::vec3* GpuClothSimulator::getNormals() const
{
    return m_normalMirror.empty() ? nullptr : m_normalMirror.data();
}

void GpuClothSimulator::buildInitialGrid(const ClothConfig& config)
{
    // CPU-side scaffolding mirrors what `ClothSimulator::initialize()` builds:
    // a flat XZ grid of particles centered at the origin with the cloth's
    // local-frame "up" along +Y. Topology (indices, UVs) is the same too so
    // the renderer is wholly oblivious to which backend is producing data.
    const uint32_t W = config.width;
    const uint32_t H = config.height;
    const float    s = config.spacing;

    m_positionMirror.resize(m_particleCount);
    m_normalMirror.resize(m_particleCount, glm::vec3(0.0f, 1.0f, 0.0f));
    m_texCoords.resize(m_particleCount);

    const float wMinus1 = static_cast<float>(W - 1);
    const float hMinus1 = static_cast<float>(H - 1);
    for (uint32_t z = 0; z < H; ++z)
    {
        for (uint32_t x = 0; x < W; ++x)
        {
            const uint32_t idx = z * W + x;
            const float fx = (static_cast<float>(x) - wMinus1 * 0.5f) * s;
            const float fz = (static_cast<float>(z) - hMinus1 * 0.5f) * s;
            m_positionMirror[idx] = glm::vec3(fx, 0.0f, fz);
            m_texCoords[idx]      = glm::vec2(static_cast<float>(x) / wMinus1,
                                               static_cast<float>(z) / hMinus1);
        }
    }

    // Two triangles per quad cell, (W-1) * (H-1) cells, 3 indices per triangle.
    m_indices.clear();
    m_indices.reserve((W - 1) * (H - 1) * 6);
    for (uint32_t z = 0; z + 1 < H; ++z)
    {
        for (uint32_t x = 0; x + 1 < W; ++x)
        {
            const uint32_t i0 = z * W + x;
            const uint32_t i1 = z * W + (x + 1);
            const uint32_t i2 = (z + 1) * W + x;
            const uint32_t i3 = (z + 1) * W + (x + 1);
            m_indices.push_back(i0); m_indices.push_back(i2); m_indices.push_back(i1);
            m_indices.push_back(i1); m_indices.push_back(i2); m_indices.push_back(i3);
        }
    }
}

void GpuClothSimulator::createBuffers()
{
    // Positions, prev positions, velocities, normals all use vec4 layout
    // (xyz + w padding / inverse mass) to satisfy std430's vec3-array padding
    // semantics. Encoding an inverse-mass channel is a Step 4 concern; for
    // now the w channel is initialised to 1.0 (signalling "free particle").
    std::vector<glm::vec4> initialVec4(m_particleCount);
    for (uint32_t i = 0; i < m_particleCount; ++i)
    {
        initialVec4[i] = glm::vec4(m_positionMirror[i], 1.0f);
    }

    const GLsizeiptr vec4Bytes = static_cast<GLsizeiptr>(m_particleCount * sizeof(glm::vec4));

    glCreateBuffers(1, &m_positionsSSBO);
    glNamedBufferStorage(m_positionsSSBO, vec4Bytes, initialVec4.data(),
                         GL_DYNAMIC_STORAGE_BIT);

    glCreateBuffers(1, &m_prevPositionsSSBO);
    glNamedBufferStorage(m_prevPositionsSSBO, vec4Bytes, initialVec4.data(),
                         GL_DYNAMIC_STORAGE_BIT);

    // Velocities start at zero.
    std::vector<glm::vec4> zeroVec4(m_particleCount, glm::vec4(0.0f));
    glCreateBuffers(1, &m_velocitiesSSBO);
    glNamedBufferStorage(m_velocitiesSSBO, vec4Bytes, zeroVec4.data(),
                         GL_DYNAMIC_STORAGE_BIT);

    // Normals: +Y for the flat starting pose.
    std::vector<glm::vec4> upVec4(m_particleCount, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    glCreateBuffers(1, &m_normalsSSBO);
    glNamedBufferStorage(m_normalsSSBO, vec4Bytes, upVec4.data(),
                         GL_DYNAMIC_STORAGE_BIT);

    // Index buffer: regular triangle list, immutable for the cloth's lifetime.
    const GLsizeiptr indexBytes =
        static_cast<GLsizeiptr>(m_indices.size() * sizeof(uint32_t));
    glCreateBuffers(1, &m_indicesSSBO);
    glNamedBufferStorage(m_indicesSSBO, indexBytes, m_indices.data(),
                         /*flags=*/0);  // Immutable.
}

void GpuClothSimulator::destroyBuffers()
{
    if (m_positionsSSBO)     { glDeleteBuffers(1, &m_positionsSSBO);     m_positionsSSBO = 0; }
    if (m_prevPositionsSSBO) { glDeleteBuffers(1, &m_prevPositionsSSBO); m_prevPositionsSSBO = 0; }
    if (m_velocitiesSSBO)    { glDeleteBuffers(1, &m_velocitiesSSBO);    m_velocitiesSSBO = 0; }
    if (m_normalsSSBO)       { glDeleteBuffers(1, &m_normalsSSBO);       m_normalsSSBO = 0; }
    if (m_indicesSSBO)       { glDeleteBuffers(1, &m_indicesSSBO);       m_indicesSSBO = 0; }
    m_initialized = false;
    m_particleCount = 0;
    m_gridW = m_gridH = 0;
    m_positionMirror.clear();
    m_normalMirror.clear();
    m_indices.clear();
    m_texCoords.clear();
}

} // namespace Vestige
