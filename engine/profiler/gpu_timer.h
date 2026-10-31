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

    /// @brief Whether getFrameSpanMs() holds a measurement for the results.
    bool hasFrameSpan() const { return m_hasFrameSpan; }

    /// @brief GPU time from the frame's start to markFrameEnd(), in ms.
    double getFrameSpanMs() const { return m_frameSpanMs; }

    static constexpr int BUFFER_COUNT = 3;

    /// @brief How many frames before the latest endFrame() the results were
    ///        submitted: they are read from the oldest buffer.
    static constexpr int RESULT_LATENCY_FRAMES = BUFFER_COUNT - 1;

private:
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
};

} // namespace Vestige
