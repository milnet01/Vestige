// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file machine_load.h
/// @brief How busy the machine was around a run, for the frame log's header
///        (3D_E-0968).
///
/// A timing taken while another program used the CPU or the GPU is not a fair
/// reading. frameledger's run-file format (FLED-0001 § 4.2) has header fields
/// for that: `load`, `gpu_busy_pct` and `gpu_clock_mhz`. This reads them from
/// the kernel's text files. Linux only so far; elsewhere every value stays
/// unknown and the header leaves the fields out.
///
/// CPU / GPU placement: CPU file reads on a sampler thread; nothing on the GPU.
#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace Vestige
{

/// @brief The header's load figures. A negative value means unknown.
struct MachineLoad
{
    /// CPU load as a share of the machine: 0 idle, 1 every logical CPU busy.
    /// Start and end are the one-minute load average, so they may exceed 1.
    double loadStart = -1.0;
    double loadEnd = -1.0;
    /// Highest CPU share OTHER programs used in any one-second sample, 0 to 1.
    double loadPeak = -1.0;

    double gpuBusyStart = -1.0;  ///< Percent, whole GPU, before the engine starts.
    double gpuBusyEnd = -1.0;    ///< Percent, whole GPU, after the last frame.
    /// Highest GPU-busy percent OTHER programs used in any one-second sample.
    double gpuBusyPeak = -1.0;

    double gpuClockStartMhz = -1.0;
    double gpuClockEndMhz = -1.0;
};

/// @brief Machine-wide CPU time from the first line of `/proc/stat`, in ticks.
struct CpuTicks
{
    unsigned long long total = 0;
    unsigned long long busy = 0;  ///< total less idle and iowait.
};

// --- Parsers of the kernel's text files. Pure, so tests need no machine. ---

/// @brief `/proc/loadavg`'s one-minute average over @p logicalCpus.
///        Negative when the text or the CPU count is unusable.
double parseLoadAverageShare(const std::string& loadavg, unsigned logicalCpus);

/// @brief The `cpu ` line of `/proc/stat`. False when it is missing or short.
bool parseProcStatCpu(const std::string& stat, CpuTicks& out);

/// @brief utime + stime of `/proc/self/stat`: this process's CPU time in ticks.
///        Reads from the last `)`, since the program name may hold spaces.
bool parseSelfStatTicks(const std::string& selfStat, unsigned long long& ticks);

/// @brief The starred line of amdgpu's `pp_dpm_sclk`, in MHz. Negative if none.
double parseGpuClockMhz(const std::string& ppDpmSclk);

/// @brief amdgpu's `gpu_busy_percent`. Negative unless it is 0 to 100.
double parseGpuBusyPercent(const std::string& text);

/// @brief One DRM `fdinfo` text: its `drm-client-id` and the sum of its
///        `drm-engine-<name>: N ns` lines. False for a file that is not a DRM
///        client's or that reports no engine time.
bool parseDrmEngineNs(const std::string& fdinfo, unsigned long long& clientId,
                      unsigned long long& engineNs);

/// @brief The share of the machine's CPU time that programs other than this
///        one used between two readings, clamped to 0..1. Negative when no
///        time passed.
double otherCpuShare(const CpuTicks& before, const CpuTicks& after,
                     unsigned long long ownTicksBefore, unsigned long long ownTicksAfter);

/// @brief Whole-GPU busy percent less this process's own share of the window,
///        clamped to 0..100. Negative when the window is empty or the
///        whole-GPU figure is unknown.
double otherGpuBusyPercent(double wholeBusyPercent, unsigned long long ownEngineNs,
                           unsigned long long windowNs);

/// @brief Takes the readings around one run.
///
/// `start()` before the engine loads anything, `beginRun()` when frames start,
/// `finish()` after the last frame and before the GPU is released. The peaks
/// come from a thread of its own, so the frame loop reads no files.
class MachineLoadSampler
{
public:
    MachineLoadSampler() = default;
    ~MachineLoadSampler();

    MachineLoadSampler(const MachineLoadSampler&) = delete;
    MachineLoadSampler& operator=(const MachineLoadSampler&) = delete;

    /// @brief Reads the start values. Blocks for about half a second: one GPU
    ///        busy reading jumps, so this keeps the highest of a few.
    void start();

    /// @brief Starts the thread that samples the peaks.
    void beginRun();

    /// @brief Stops the thread, reads the end values and returns everything.
    ///        Safe to call twice; the second call returns the same figures.
    MachineLoad finish();

    bool isSampling() const { return m_thread.joinable(); }

private:
    void samplePeaks();

    MachineLoad m_load;  ///< The thread writes the peaks; read after the join.
    std::string m_gpuDevicePath;  ///< `/sys/class/drm/cardN/device`, or empty.
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    bool m_finished = false;
};

} // namespace Vestige
