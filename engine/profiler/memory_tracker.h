/// @file memory_tracker.h
/// @brief CPU heap and GPU VRAM usage tracking.
#pragma once

#include <atomic>
#include <cstddef>

namespace Vestige
{

/// @brief Tracks CPU heap allocation stats and GPU VRAM usage.
///
/// CPU tracking uses atomic counters incremented by custom new/delete overrides.
/// GPU VRAM is read from AMD sysfs (amortized — every 60 frames).
class MemoryTracker
{
public:
    /// @brief Updates GPU VRAM stats (amortized). Call once per frame.
    void update();

    // --- CPU stats ---

    /// @brief Total bytes currently allocated on the CPU heap.
    static size_t getCpuAllocatedBytes();

    /// @brief Total number of live allocations.
    static size_t getCpuAllocationCount();

    /// @brief Peak CPU allocation since start.
    static size_t getCpuPeakBytes();

    /// @brief Called by global operator new.
    static void recordAlloc(size_t bytes);

    /// @brief Called by global operator delete.
    static void recordFree(size_t bytes);

    // --- GPU stats ---

    /// @brief GPU VRAM currently used (MB).
    size_t getGpuUsedMB() const { return m_gpuUsedMB; }

    /// @brief GPU VRAM total capacity (MB).
    size_t getGpuTotalMB() const { return m_gpuTotalMB; }

private:
    void readGpuVram();

    size_t m_gpuUsedMB = 0;
    size_t m_gpuTotalMB = 0;
    int m_updateCounter = 0;
    static constexpr int GPU_UPDATE_INTERVAL = 60;

    // Static atomic counters for thread-safe heap tracking
    static std::atomic<size_t> s_allocatedBytes;
    static std::atomic<size_t> s_allocationCount;
    static std::atomic<size_t> s_peakBytes;
};

} // namespace Vestige
