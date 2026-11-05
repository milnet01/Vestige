// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file memory_tracker.cpp
/// @brief MemoryTracker implementation — CPU heap counters, this process's
///        VRAM from DRM fdinfo, and the card's total from AMD sysfs.
#include "profiler/memory_tracker.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

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
    // Clamp at zero on double-free / free-without-alloc. Plain fetch_sub would
    // wrap these atomics to SIZE_MAX and render every downstream display
    // nonsensical.
    size_t current = s_allocatedBytes.load(std::memory_order_relaxed);
    size_t desired = 0;
    do
    {
        desired = (bytes > current) ? 0 : (current - bytes);
    } while (!s_allocatedBytes.compare_exchange_weak(
        current, desired, std::memory_order_relaxed));

    size_t count = s_allocationCount.load(std::memory_order_relaxed);
    while (count > 0
           && !s_allocationCount.compare_exchange_weak(
                   count, count - 1, std::memory_order_relaxed))
    {
    }
}

std::optional<MemoryTracker::DrmClientVram> MemoryTracker::parseDrmFdinfo(const std::string& text)
{
    DrmClientVram out;
    bool hasClient = false;
    bool hasVram = false;
    bool hasResident = false;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
    {
        const auto colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        const std::string key = line.substr(0, colon);
        std::istringstream value(line.substr(colon + 1));
        if (key == "drm-pdev")
        {
            value >> out.device;
        }
        else if (key == "drm-client-id")
        {
            value >> out.clientId;
            hasClient = !out.clientId.empty();
        }
        else if (key == "drm-resident-vram" || (key == "drm-memory-vram" && !hasResident))
        {
            uint64_t amount = 0;
            std::string unit;
            if (!(value >> amount))
            {
                continue;
            }
            value >> unit;
            uint64_t scale = 1;
            if (unit == "KiB")
            {
                scale = 1024ULL;
            }
            else if (unit == "MiB")
            {
                scale = 1024ULL * 1024ULL;
            }
            else if (!unit.empty())
            {
                continue;  // A unit this parser does not know: ignore the key.
            }
            out.vramBytes = amount * scale;
            hasVram = true;
            hasResident = hasResident || key == "drm-resident-vram";
        }
    }
    if (!hasClient || !hasVram)
    {
        return std::nullopt;
    }
    return out;
}

std::optional<uint64_t> MemoryTracker::processVramBytes(const std::vector<std::string>& fdinfoTexts)
{
    // Several fds can share one DRM client (dup, or a reopened render node);
    // the kernel says to count each client id once per device.
    std::set<std::pair<std::string, std::string>> seen;
    uint64_t sum = 0;
    bool any = false;
    for (const std::string& text : fdinfoTexts)
    {
        const auto client = parseDrmFdinfo(text);
        if (!client)
        {
            continue;
        }
        any = true;
        if (seen.insert({client->device, client->clientId}).second)
        {
            sum += client->vramBytes;
        }
    }
    if (!any)
    {
        return std::nullopt;
    }
    return sum;
}

void MemoryTracker::readGpuVram()
{
#ifdef VESTIGE_PLATFORM_LINUX
    // Used: this process's own VRAM, from its DRM clients' fdinfo records.
    // amdgpu's mem_info_vram_used is card-wide, so a browser tab counted as
    // engine memory (3D_E-0820).
    std::vector<std::string> records;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fdinfo", ec))
    {
        std::ifstream in(entry.path());
        std::ostringstream text;
        text << in.rdbuf();
        if (text.str().find("drm-client-id") != std::string::npos)
        {
            records.push_back(text.str());
        }
    }
    const auto usedBytes = processVramBytes(records);
    m_gpuUsedKnown = usedBytes.has_value();
    m_gpuUsedMB = usedBytes ? static_cast<size_t>(*usedBytes / (1024ULL * 1024ULL)) : 0;

    // Total: the card's capacity, the panel's denominator. Look for the first
    // AMD GPU's sysfs node (works on RDNA2 / amdgpu driver).
    FILE* totalFile = fopen("/sys/class/drm/card1/device/mem_info_vram_total", "r");
    if (!totalFile)
    {
        totalFile = fopen("/sys/class/drm/card0/device/mem_info_vram_total", "r");
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
