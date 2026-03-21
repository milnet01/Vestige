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

    /// @brief Pushes a named scope onto the profiling stack.
    void pushScope(const char* name);

    /// @brief Pops the current scope, recording its end time.
    void popScope();

    /// @brief Gets the profiling data from the last completed frame.
    const std::vector<ProfileEntry>& getLastFrame() const { return m_lastFrame; }

    /// @brief Total CPU frame time (from beginFrame to endFrame).
    float getTotalCpuTimeMs() const { return m_lastFrameTimeMs; }

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_frameStart;
    std::vector<ProfileEntry> m_currentFrame;
    std::vector<ProfileEntry> m_lastFrame;
    std::vector<int> m_scopeStack;  ///< Index stack for parent tracking.
    float m_lastFrameTimeMs = 0.0f;
};

/// @brief RAII helper — pushes scope on construct, pops on destruct.
class CpuProfileScope
{
public:
    CpuProfileScope(const char* name);
    ~CpuProfileScope();
};

/// @brief Global CPU profiler instance.
CpuProfiler& getCpuProfiler();

} // namespace Vestige

/// @brief Place at function/scope start to measure CPU time.
#define VESTIGE_PROFILE_SCOPE(name) \
    ::Vestige::CpuProfileScope _vestigeProfileScope##__LINE__(name)
