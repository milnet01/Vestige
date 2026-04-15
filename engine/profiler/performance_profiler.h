// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file performance_profiler.h
/// @brief Central performance profiling hub — aggregates GPU, CPU, and memory data.
#pragma once

#include "profiler/gpu_timer.h"
#include "profiler/cpu_profiler.h"
#include "profiler/memory_tracker.h"

namespace Vestige
{

/// @brief Central hub for all performance profiling subsystems.
///
/// Owns the GPU timer, references the global CPU profiler, and manages
/// the memory tracker. Maintains a frame time ring buffer for history display.
class PerformanceProfiler
{
public:
    PerformanceProfiler() = default;

    /// @brief Initializes all profiling subsystems.
    void init();

    /// @brief Shuts down all profiling subsystems.
    void shutdown();

    /// @brief Call at the start of each frame.
    void beginFrame();

    /// @brief Call at the end of each frame (after swap).
    /// @param deltaTime Frame time in seconds.
    void endFrame(float deltaTime);

    /// @brief Gets the GPU timer for inserting pass markers.
    GpuTimer& getGpuTimer() { return m_gpuTimer; }

    /// @brief Gets the CPU profiler.
    CpuProfiler& getCpuProfiler() { return getCpuProfiler_(); }

    /// @brief Gets the memory tracker.
    MemoryTracker& getMemoryTracker() { return m_memoryTracker; }

    // --- Frame time history ---

    /// @brief Current frame time in milliseconds.
    float getFrameTimeMs() const { return m_currentFrameTimeMs; }

    /// @brief Current FPS.
    float getFps() const;

    /// @brief Gets the frame time history ring buffer (HISTORY_SIZE floats).
    const float* getFrameTimeHistory() const { return m_frameTimeHistory; }

    /// @brief Size of the history buffer.
    static constexpr int HISTORY_SIZE = 300;

    /// @brief Current write index into the history buffer.
    int getHistoryOffset() const { return m_historyIndex; }

    // --- Statistics ---

    /// @brief Average frame time over the history window.
    float getAvgFrameTimeMs() const { return m_avgFrameTime; }

    /// @brief Minimum frame time over the history window.
    float getMinFrameTimeMs() const { return m_minFrameTime; }

    /// @brief Maximum frame time over the history window.
    float getMaxFrameTimeMs() const { return m_maxFrameTime; }

    /// @brief Whether profiling is enabled.
    bool isEnabled() const { return m_enabled; }

    /// @brief Enable/disable profiling. When disabled, GPU queries are skipped.
    void setEnabled(bool enabled) { m_enabled = enabled; }

private:
    CpuProfiler& getCpuProfiler_() { return Vestige::getCpuProfiler(); }

    GpuTimer m_gpuTimer;
    MemoryTracker m_memoryTracker;

    float m_frameTimeHistory[HISTORY_SIZE] = {};
    int m_historyIndex = 0;
    float m_currentFrameTimeMs = 0.0f;
    float m_avgFrameTime = 0.0f;
    float m_minFrameTime = 999.0f;
    float m_maxFrameTime = 0.0f;

    bool m_enabled = false;
};

} // namespace Vestige
