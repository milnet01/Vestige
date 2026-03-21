/// @file performance_profiler.cpp
/// @brief PerformanceProfiler implementation — frame time tracking and subsystem coordination.
#include "profiler/performance_profiler.h"

#include <algorithm>

namespace Vestige
{

void PerformanceProfiler::init()
{
    m_gpuTimer.init();
}

void PerformanceProfiler::shutdown()
{
    m_gpuTimer.shutdown();
}

void PerformanceProfiler::beginFrame()
{
    if (m_enabled)
    {
        m_gpuTimer.beginFrame();
        getCpuProfiler_().beginFrame();
    }
}

void PerformanceProfiler::endFrame(float deltaTime)
{
    m_currentFrameTimeMs = deltaTime * 1000.0f;

    if (m_enabled)
    {
        getCpuProfiler_().endFrame();
        m_gpuTimer.endFrame();
        m_memoryTracker.update();
    }

    // Always update frame time history (even when profiler panel is hidden)
    m_frameTimeHistory[m_historyIndex] = m_currentFrameTimeMs;
    m_historyIndex = (m_historyIndex + 1) % HISTORY_SIZE;

    // Compute stats from history
    float sum = 0.0f;
    float minVal = 999.0f;
    float maxVal = 0.0f;
    for (int i = 0; i < HISTORY_SIZE; ++i)
    {
        float v = m_frameTimeHistory[i];
        if (v <= 0.0f) continue;  // Skip uninitialized slots
        sum += v;
        minVal = std::min(minVal, v);
        maxVal = std::max(maxVal, v);
    }

    m_avgFrameTime = sum / HISTORY_SIZE;
    m_minFrameTime = (minVal < 999.0f) ? minVal : 0.0f;
    m_maxFrameTime = maxVal;
}

float PerformanceProfiler::getFps() const
{
    if (m_currentFrameTimeMs > 0.0f)
    {
        return 1000.0f / m_currentFrameTimeMs;
    }
    return 0.0f;
}

} // namespace Vestige
