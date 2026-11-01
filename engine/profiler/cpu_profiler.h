// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cpu_profiler.h
/// @brief RAII scope-based CPU profiler with hierarchical flat array storage.
#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief A single profiling entry recorded during a frame.
struct ProfileEntry
{
    const char* name = nullptr;  ///< String literal — no allocation.
    float startMs = 0.0f;        ///< Start time relative to frame start.
    float endMs = 0.0f;          ///< End time relative to frame start.
    int parentIndex = -1;        ///< Index of parent scope (-1 = root).
    int depth = 0;               ///< Nesting depth (0 = top level).
};

/// @brief Records CPU scope timings per frame with hierarchical nesting.
///
/// Use VESTIGE_PROFILE_SCOPE("name") at function/block start.
/// Data from the last completed frame is available via getLastFrame().
class CpuProfiler
{
public:
    /// @brief Marks the start of a new frame. Resets current-frame data.
    void beginFrame();

    /// @brief Marks the end of a frame. Snapshots data for display.
    void endFrame();

    /// @brief Marks the end of the frame's work, before the swap and any
    ///        frame-cap wait. Optional; without it getWorkTimeMs() equals
    ///        getTotalCpuTimeMs() (3D_E-0789).
    void markWorkEnd();

    /// @brief Pushes a named scope onto the profiling stack.
    void pushScope(const char* name);

    /// @brief Pops the current scope, recording its end time.
    void popScope();

    /// @brief Gets the profiling data from the last completed frame.
    const std::vector<ProfileEntry>& getLastFrame() const { return m_lastFrame; }

    /// @brief Total CPU frame time (from beginFrame to endFrame).
    float getTotalCpuTimeMs() const { return m_lastFrameTimeMs; }

    /// @brief CPU time from beginFrame to markWorkEnd: the frame's own work,
    ///        without vsync or frame-cap waits. Use this, not the total, to
    ///        decide CPU-bound against GPU-bound.
    float getWorkTimeMs() const { return m_lastWorkTimeMs; }

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_frameStart;
    std::vector<ProfileEntry> m_currentFrame;
    std::vector<ProfileEntry> m_lastFrame;
    std::vector<int> m_scopeStack;  ///< Index stack for parent tracking.
    TimePoint m_workEnd;
    bool m_workEndMarked = false;
    float m_lastFrameTimeMs = 0.0f;
    float m_lastWorkTimeMs = 0.0f;
};

/// @brief RAII helper — pushes scope on construct, pops on destruct.
class CpuProfileScope
{
public:
    explicit CpuProfileScope(const char* name);
    ~CpuProfileScope();
};

/// @brief Global CPU profiler instance.
CpuProfiler& getCpuProfiler();

} // namespace Vestige

/// @brief Place at function/scope start to measure CPU time.
#define VESTIGE_CONCAT_IMPL_(a, b) a##b
#define VESTIGE_CONCAT_(a, b) VESTIGE_CONCAT_IMPL_(a, b)
#define VESTIGE_PROFILE_SCOPE(name) \
    ::Vestige::CpuProfileScope VESTIGE_CONCAT_(_vestigeProfileScope, __LINE__)(name)
