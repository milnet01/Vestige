// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file memory_tracker.cpp
/// @brief MemoryTracker implementation — CPU heap counters + AMD sysfs VRAM.
#include "profiler/memory_tracker.h"

#include <cstdio>

namespace Vestige
{

// Static atomic counters
std::atomic<size_t> MemoryTracker::s_allocatedBytes{0};
std::atomic<size_t> MemoryTracker::s_allocationCount{0};
std::atomic<size_t> MemoryTracker::s_peakBytes{0};

void MemoryTracker::update()
{
    ++m_updateCounter;
    if (m_updateCounter >= GPU_UPDATE_INTERVAL)
    {
        m_updateCounter = 0;
        readGpuVram();
    }
}

size_t MemoryTracker::getCpuAllocatedBytes()
{
    return s_allocatedBytes.load(std::memory_order_relaxed);
}

size_t MemoryTracker::getCpuAllocationCount()
{
    return s_allocationCount.load(std::memory_order_relaxed);
}

size_t MemoryTracker::getCpuPeakBytes()
{
    return s_peakBytes.load(std::memory_order_relaxed);
}

void MemoryTracker::recordAlloc(size_t bytes)
{
    size_t current = s_allocatedBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    s_allocationCount.fetch_add(1, std::memory_order_relaxed);

    // Update peak (lock-free CAS loop)
    size_t peak = s_peakBytes.load(std::memory_order_relaxed);
    while (current > peak)
    {
        if (s_peakBytes.compare_exchange_weak(peak, current, std::memory_order_relaxed))
        {
            break;
        }
    }
}

void MemoryTracker::recordFree(size_t bytes)
{
    s_allocatedBytes.fetch_sub(bytes, std::memory_order_relaxed);
    s_allocationCount.fetch_sub(1, std::memory_order_relaxed);
}

void MemoryTracker::readGpuVram()
{
#ifdef VESTIGE_PLATFORM_LINUX
    // Try AMD sysfs (works on RDNA2 / amdgpu driver)
    // Look for the first AMD GPU's VRAM usage
    FILE* usedFile = fopen("/sys/class/drm/card1/device/mem_info_vram_used", "r");
    FILE* totalFile = fopen("/sys/class/drm/card1/device/mem_info_vram_total", "r");

    if (!usedFile || !totalFile)
    {
        // Close any partially opened card1 handles before falling back to card0
        if (usedFile) { fclose(usedFile); usedFile = nullptr; }
        if (totalFile) { fclose(totalFile); totalFile = nullptr; }
        usedFile = fopen("/sys/class/drm/card0/device/mem_info_vram_used", "r");
        totalFile = fopen("/sys/class/drm/card0/device/mem_info_vram_total", "r");
    }

    if (usedFile)
    {
        unsigned long long used = 0;
        if (fscanf(usedFile, "%llu", &used) == 1)
        {
            m_gpuUsedMB = static_cast<size_t>(used / (1024ULL * 1024ULL));
        }
        fclose(usedFile);
    }

    if (totalFile)
    {
        unsigned long long total = 0;
        if (fscanf(totalFile, "%llu", &total) == 1)
        {
            m_gpuTotalMB = static_cast<size_t>(total / (1024ULL * 1024ULL));
        }
        fclose(totalFile);
    }
#endif
}

} // namespace Vestige
