// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file depth_reducer.cpp
/// @brief GPU depth buffer min/max reduction implementation.
#include "renderer/depth_reducer.h"
#include "core/logger.h"

#include <cmath>
#include <cstring>

namespace Vestige
{

// SSBO layout: two uint32 values (minDepthBits, maxDepthBits)
static constexpr GLsizeiptr SSBO_SIZE = 2 * sizeof(uint32_t);

DepthReducer::DepthReducer() = default;

DepthReducer::~DepthReducer()
{
    for (int i = 0; i < 2; i++)
    {
        if (m_ssbo[i] != 0)
        {
            glDeleteBuffers(1, &m_ssbo[i]);
        }
    }
}

bool DepthReducer::init(const std::string& shaderPath)
{
    if (!m_computeShader.loadComputeShader(shaderPath))
    {
        Logger::error("DepthReducer: failed to load compute shader: " + shaderPath);
        return false;
    }

    // Create double-buffered SSBOs with initial values.
    // minDepthBits = 0xFFFFFFFF (max uint → min will reduce downward)
    // maxDepthBits = 0x00000000 (min uint → max will reduce upward)
    uint32_t initData[2] = {0xFFFFFFFFu, 0x00000000u};

    for (int i = 0; i < 2; i++)
    {
        glCreateBuffers(1, &m_ssbo[i]);
        glNamedBufferStorage(m_ssbo[i], SSBO_SIZE, initData,
            GL_MAP_READ_BIT | GL_DYNAMIC_STORAGE_BIT);
    }

    Logger::info("SDSM depth reducer initialized");
    return true;
}

void DepthReducer::dispatch(GLuint depthTexture, int width, int height)
{
    // Reset the write SSBO to initial sentinel values
    uint32_t resetData[2] = {0xFFFFFFFFu, 0x00000000u};
    glNamedBufferSubData(m_ssbo[m_writeIndex], 0, SSBO_SIZE, resetData);

    // Bind the SSBO for the compute shader
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo[m_writeIndex]);

    // Bind depth texture to unit 0
    glBindTextureUnit(0, depthTexture);

    // Dispatch compute shader
    m_computeShader.use();

    int groupsX = (width + 15) / 16;
    int groupsY = (height + 15) / 16;
    glDispatchCompute(static_cast<GLuint>(groupsX),
                      static_cast<GLuint>(groupsY), 1);

    // Memory barrier so CPU reads in future frames see the compute results
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    // Swap buffers for next frame
    m_writeIndex = 1 - m_writeIndex;
    m_frameCount++;
}

bool DepthReducer::readBounds(float cameraNear, float& outNear, float& outFar)
{
    // Need at least 2 frames: one to dispatch, one to read the result
    if (m_frameCount < 2)
    {
        return false;
    }

    // Read from the SSBO that was written in the PREVIOUS frame.
    // After dispatch swapped m_writeIndex, the read index is the current m_writeIndex
    // (which was the write target last frame, and is now the read target).
    int readIndex = m_writeIndex;  // This was swapped after last dispatch

    // R8 (Phase 10.9 Slice 4): use `glMapNamedBufferRange` instead of
    // `glGetNamedBufferSubData`. The latter blocks the main thread
    // until every pending GPU op affecting the buffer has flushed —
    // a hard stall that blocked 60 FPS on Mesa AMD. With the
    // existing double-buffering (m_writeIndex swapped after each
    // dispatch), the read target's last write happened ≥ 1 frame
    // ago and the GPU has flushed; map then returns the data
    // without sync. Same pattern as the bloom luminance readback
    // at renderer.cpp:1113-1158.
    auto* mapped = static_cast<const uint32_t*>(
        glMapNamedBufferRange(m_ssbo[readIndex], 0, SSBO_SIZE, GL_MAP_READ_BIT));
    if (!mapped)
    {
        // Map failed (driver pressure or buffer in unexpected state).
        // Bail without returning bounds; next frame retries.
        return false;
    }

    uint32_t minDepthBits = mapped[0];
    uint32_t maxDepthBits = mapped[1];

    glUnmapNamedBuffer(m_ssbo[readIndex]);

    // Check for sentinel values (no geometry was found)
    if (minDepthBits == 0xFFFFFFFFu || maxDepthBits == 0x00000000u)
    {
        return false;
    }

    // Convert bits back to float depth values
    float minDepth = 0.0f;
    float maxDepth = 0.0f;
    std::memcpy(&minDepth, &minDepthBits, sizeof(float));
    std::memcpy(&maxDepth, &maxDepthBits, sizeof(float));

    // Validate depth range
    if (minDepth <= 0.0f || maxDepth <= 0.0f || minDepth > 1.0f || maxDepth > 1.0f)
    {
        return false;
    }

    // Reverse-Z infinite projection: z_linear = near / depth
    // minDepth (smallest depth) = farthest geometry
    // maxDepth (largest depth)  = nearest geometry
    outFar = cameraNear / minDepth;
    outNear = cameraNear / maxDepth;

    // Sanity check
    if (!std::isfinite(outNear) || !std::isfinite(outFar) || outNear >= outFar)
    {
        return false;
    }

    return true;
}

} // namespace Vestige
