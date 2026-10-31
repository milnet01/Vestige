// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_timer.cpp
/// @brief GpuTimer implementation — triple-buffered GL_TIMESTAMP queries.
#include "profiler/gpu_timer.h"

namespace Vestige
{

GpuTimer::~GpuTimer()
{
    shutdown();
}

void GpuTimer::init()
{
    if (m_initialized) return;

    for (int buf = 0; buf < BUFFER_COUNT; ++buf)
    {
        glGenQueries(MAX_PASSES * 2, m_queries[buf]);
        glGenQueries(2, m_spanQueries[buf]);
    }

    m_initialized = true;
    m_frameCount = 0;
    m_writeBuffer = 0;
}

void GpuTimer::shutdown()
{
    if (!m_initialized) return;

    for (int buf = 0; buf < BUFFER_COUNT; ++buf)
    {
        glDeleteQueries(MAX_PASSES * 2, m_queries[buf]);
        glDeleteQueries(2, m_spanQueries[buf]);
        m_spanQueries[buf][0] = 0;
        m_spanQueries[buf][1] = 0;
        m_spanEnded[buf] = false;
        for (int i = 0; i < MAX_PASSES * 2; ++i)
        {
            m_queries[buf][i] = 0;
        }
    }

    m_initialized = false;
}

void GpuTimer::beginFrame()
{
    m_passCount = 0;
    if (!m_initialized) return;

    m_spanEnded[m_writeBuffer] = false;
    glQueryCounter(m_spanQueries[m_writeBuffer][0], GL_TIMESTAMP);
}

void GpuTimer::markFrameEnd()
{
    if (!m_initialized) return;

    glQueryCounter(m_spanQueries[m_writeBuffer][1], GL_TIMESTAMP);
    m_spanEnded[m_writeBuffer] = true;
}

void GpuTimer::beginPass(const char* name)
{
    if (!m_initialized || m_passCount >= MAX_PASSES) return;

    m_passNames[m_writeBuffer][m_passCount] = name;
    glQueryCounter(m_queries[m_writeBuffer][m_passCount * 2], GL_TIMESTAMP);
}

void GpuTimer::endPass()
{
    if (!m_initialized || m_passCount >= MAX_PASSES) return;

    glQueryCounter(m_queries[m_writeBuffer][m_passCount * 2 + 1], GL_TIMESTAMP);
    ++m_passCount;
}

void GpuTimer::endFrame()
{
    if (!m_initialized) return;

    ++m_frameCount;

    // Read results from the oldest buffer (2 frames ago)
    m_readBuffer = (m_writeBuffer + 1) % BUFFER_COUNT;

    // Store the pass count for the buffer we just wrote
    m_bufferPassCount[m_writeBuffer] = m_passCount;

    if (m_frameCount >= BUFFER_COUNT)
    {
        m_results.clear();
        // Read the exact pass count that was recorded in the read buffer
        int readPassCount = m_bufferPassCount[m_readBuffer];

        for (int i = 0; i < readPassCount; ++i)
        {
            GLuint64 startTime = 0;
            GLuint64 endTime = 0;

            glGetQueryObjectui64v(m_queries[m_readBuffer][i * 2],
                                  GL_QUERY_RESULT, &startTime);
            glGetQueryObjectui64v(m_queries[m_readBuffer][i * 2 + 1],
                                  GL_QUERY_RESULT, &endTime);

            GpuPassTiming timing;
            timing.name = m_passNames[m_readBuffer][i] ? m_passNames[m_readBuffer][i] : "Unknown";
            timing.timeMs = static_cast<float>(endTime - startTime) / 1e6f;
            m_results.push_back(timing);
        }

        m_hasFrameSpan = m_spanEnded[m_readBuffer];
        if (m_hasFrameSpan)
        {
            GLuint64 startTime = 0;
            GLuint64 endTime = 0;
            glGetQueryObjectui64v(m_spanQueries[m_readBuffer][0], GL_QUERY_RESULT, &startTime);
            glGetQueryObjectui64v(m_spanQueries[m_readBuffer][1], GL_QUERY_RESULT, &endTime);
            m_frameSpanMs = static_cast<double>(endTime - startTime) / 1e6;
        }
    }

    // Advance write buffer
    m_writeBuffer = (m_writeBuffer + 1) % BUFFER_COUNT;
}

float GpuTimer::getTotalGpuTimeMs() const
{
    float total = 0.0f;
    for (const auto& r : m_results)
    {
        total += r.timeMs;
    }
    return total;
}

} // namespace Vestige
