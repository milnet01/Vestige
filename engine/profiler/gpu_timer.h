// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_timer.h
/// @brief Double-buffered GL_TIMESTAMP query pool for per-pass GPU timing.
#pragma once

#include <glad/gl.h>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Timing result for a single GPU pass.
struct GpuPassTiming
{
    std::string name;
    float timeMs = 0.0f;
};

/// @brief Manages OpenGL timestamp queries to measure per-pass GPU time.
///
/// Uses triple-buffered queries so we read results from 2 frames ago,
/// avoiding pipeline stalls. Insert beginPass/endPass around each render pass.
class GpuTimer
{
public:
    GpuTimer() = default;
    ~GpuTimer();

    // Non-copyable
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    /// @brief Creates the GL query objects.
    void init();

    /// @brief Destroys GL query objects.
    void shutdown();

    /// @brief Resets pass count for a new frame. Call at frame start.
    void beginFrame();

    /// @brief Begins timing a named GPU pass.
    void beginPass(const char* name);

    /// @brief Ends the current GPU pass timing.
    void endPass();

    /// @brief Ends the frame's whole-frame span. Call after the frame's last GL
    ///        work and before the swap. Passes miss work outside any pass; this
    ///        does not.
    void markFrameEnd();

    /// @brief Collects results and swaps buffers. Call at frame end.
    void endFrame();

    /// @brief Gets the timing results from the last completed frame.
    const std::vector<GpuPassTiming>& getResults() const { return m_results; }

    /// @brief Total GPU frame time in ms (sum of all passes).
    float getTotalGpuTimeMs() const;

    /// @brief Whether the timer has valid results (needs 3 frames to warm up).
    bool hasResults() const { return m_frameCount >= BUFFER_COUNT; }

    /// @brief Whether the latest endFrame() read a new sample: the results are
    ///        exactly RESULT_LATENCY_FRAMES frames old. False when the GPU had
    ///        not finished that frame's queries and the sample was skipped; the
    ///        results then still hold the previous sample (3D_E-0788).
    bool hasFreshResults() const { return m_resultsFresh; }

    /// @brief Passes begun while another was open, plus unmatched endPass
    ///        calls, since init(). Each is ignored and logged once (3D_E-0790).
    int getNestingViolations() const { return m_nestingViolations; }

    /// @brief Passes dropped because more than MAX_PASSES were begun in one
    ///        frame, since init(). Logged once (3D_E-0790).
    int getDroppedPasses() const { return m_droppedPasses; }

    /// @brief Frame span minus the sum of the passes, never below zero: GPU
    ///        work that no pass covers (water re-renders, UI, blit, ImGui).
    ///        Zero when there is no span.
    double getUnattributedMs() const;

    /// @brief The arithmetic behind getUnattributedMs().
    static double unattributedMs(double spanMs, double passSumMs)
    {
        return spanMs > passSumMs ? spanMs - passSumMs : 0.0;
    }

    /// @brief Whether getFrameSpanMs() holds a measurement for the results.
    bool hasFrameSpan() const { return m_hasFrameSpan; }

    /// @brief GPU time from the frame's start to markFrameEnd(), in ms.
    double getFrameSpanMs() const { return m_frameSpanMs; }

    static constexpr int BUFFER_COUNT = 3;

    /// @brief How many frames before the latest endFrame() the results were
    ///        submitted: they are read from the oldest buffer.
    static constexpr int RESULT_LATENCY_FRAMES = BUFFER_COUNT - 1;

private:
    /// @brief Pops the debug group of every pass still open, and resets the depth.
    void closeOpenDebugGroups();

    static constexpr int MAX_PASSES = 16;

    GLuint m_queries[BUFFER_COUNT][MAX_PASSES * 2] = {};  // start+end per pass
    GLuint m_spanQueries[BUFFER_COUNT][2] = {};  // whole-frame start+end
    bool m_spanEnded[BUFFER_COUNT] = {};  // markFrameEnd() ran for this buffer
    const char* m_passNames[BUFFER_COUNT][MAX_PASSES] = {};  // per-buffer pass names
    int m_passCount = 0;
    int m_bufferPassCount[BUFFER_COUNT] = {};  // passes recorded per buffer
    int m_writeBuffer = 0;
    int m_readBuffer = 0;
    int m_frameCount = 0;
    bool m_initialized = false;

    std::vector<GpuPassTiming> m_results;
    double m_frameSpanMs = 0.0;
    bool m_hasFrameSpan = false;
    bool m_resultsFresh = false;

    // Pass nesting guard (3D_E-0790). The timer is flat: a nested pair would
    // overwrite the outer slot, so it is refused and counted instead.
    int m_depth = 0;               ///< beginPass calls not yet matched by endPass
    bool m_openPassDropped = false;  ///< the open outer pass hit MAX_PASSES
    int m_nestingViolations = 0;
    int m_droppedPasses = 0;
    bool m_loggedNesting = false;
    bool m_loggedDropped = false;

    /// True when the GPU has finished the queries of buffer `buf`, so reading
    /// them cannot make the CPU wait. Timestamps finish in order, so the last
    /// one issued is enough.
    bool buffersReady(int buf) const;
};

} // namespace Vestige
