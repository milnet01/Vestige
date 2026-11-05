// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file memory_tracker.h
/// @brief CPU heap and GPU VRAM usage tracking.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Tracks CPU heap allocation stats and GPU VRAM usage.
///
/// CPU tracking uses atomic counters fed by `recordAlloc` / `recordFree`.
/// No global new/delete override in the engine calls them, so outside tests
/// the CPU figures stay at zero. The Performance panel therefore shows only
/// GPU VRAM (3D_E-0787).
/// GPU VRAM used is this process's own, summed from its DRM clients'
/// /proc/self/fdinfo records (Linux); the card's total comes from AMD sysfs.
/// Both are read every 60 frames. Elsewhere, or on a kernel without DRM
/// fdinfo memory keys, the used figure is unknown (3D_E-0820).
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

    /// @brief Records an allocation. Nothing in the engine calls this yet.
    static void recordAlloc(size_t bytes);

    /// @brief Records a free. Nothing in the engine calls this yet.
    static void recordFree(size_t bytes);

    // --- GPU stats ---

    /// @brief GPU VRAM this process uses (MB). Meaningful only when
    ///        hasGpuUsed() is true.
    size_t getGpuUsedMB() const { return m_gpuUsedMB; }

    /// @brief Whether the process's own VRAM use could be read.
    bool hasGpuUsed() const { return m_gpuUsedKnown; }

    /// @brief GPU VRAM total capacity (MB).
    size_t getGpuTotalMB() const { return m_gpuTotalMB; }

    /// @brief One DRM client's VRAM, from a /proc/<pid>/fdinfo/<fd> record.
    struct DrmClientVram
    {
        std::string device;      ///< drm-pdev (PCI address)
        std::string clientId;    ///< drm-client-id: fds sharing it are one client
        uint64_t vramBytes = 0;  ///< drm-resident-vram, else drm-memory-vram (older kernels)
    };

    /// @brief Parses one fdinfo record. Empty when the fd is not a DRM client
    ///        or has no VRAM key. Units follow the kernel's drm-usage-stats:
    ///        no suffix is bytes, else KiB or MiB.
    static std::optional<DrmClientVram> parseDrmFdinfo(const std::string& text);

    /// @brief Sums VRAM over fdinfo records, counting each (device, client)
    ///        once. Empty when no record was a DRM client with a VRAM key.
    static std::optional<uint64_t> processVramBytes(const std::vector<std::string>& fdinfoTexts);

private:
    void readGpuVram();

    size_t m_gpuUsedMB = 0;
    bool m_gpuUsedKnown = false;
    size_t m_gpuTotalMB = 0;
    int m_updateCounter = 0;
    static constexpr int GPU_UPDATE_INTERVAL = 60;

    // Static atomic counters for thread-safe heap tracking
    static std::atomic<size_t> s_allocatedBytes;
    static std::atomic<size_t> s_allocationCount;
    static std::atomic<size_t> s_peakBytes;
};

} // namespace Vestige
