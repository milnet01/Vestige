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

    /// @brief Collects results and swaps buffers. Call at frame end.
    void endFrame();

    /// @brief Gets the timing results from the last completed frame.
    const std::vector<GpuPassTiming>& getResults() const { return m_results; }

    /// @brief Total GPU frame time in ms (sum of all passes).
    float getTotalGpuTimeMs() const;

    /// @brief Whether the timer has valid results (needs 3 frames to warm up).
    bool hasResults() const { return m_frameCount >= BUFFER_COUNT; }

private:
    static constexpr int MAX_PASSES = 16;
    static constexpr int BUFFER_COUNT = 3;

    GLuint m_queries[BUFFER_COUNT][MAX_PASSES * 2] = {};  // start+end per pass
    const char* m_passNames[MAX_PASSES] = {};
    int m_passCount = 0;
    int m_bufferPassCount[BUFFER_COUNT] = {};  // passes recorded per buffer
    int m_writeBuffer = 0;
    int m_readBuffer = 0;
    int m_frameCount = 0;
    bool m_initialized = false;

    std::vector<GpuPassTiming> m_results;
};

} // namespace Vestige
