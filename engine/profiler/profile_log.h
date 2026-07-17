// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file profile_log.h
/// @brief Throttled CSV logger for profiler timings (meadow design §8.1).
///
/// So bottlenecks can be analysed off-panel and shared, a small `ProfileLog`
/// appends the profiler's per-pass GPU/CPU/memory timings to a CSV file at
/// ~1 Hz (interval averages, so the file stays small and single-frame noise is
/// smoothed). Off unless a path is opened — zero cost on a normal launch.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace Vestige
{

class PerformanceProfiler;

/// @brief CSV row category — maps to the `category` column string.
enum class ProfileCategory
{
    Frame,  ///< "frame"
    Gpu,    ///< "gpu"
    Cpu,    ///< "cpu"
    Mem,    ///< "mem"
};

/// @brief One already-reduced sample entry (carries no timing math).
struct ProfileSampleEntry
{
    ProfileCategory category = ProfileCategory::Frame;
    std::string name;    ///< Pass / scope name, or "total" / "gpu_mb".
    int depth = 0;       ///< CPU-scope nesting; 0 for gpu / frame / mem / totals.
    double value = 0.0;  ///< ms (frame/gpu/cpu) or MB (mem — the `ms` column).
};

/// @brief A single throttled sample: top-level time + fps + reduced entries.
struct ProfileSample
{
    double timeSec = 0.0;  ///< Seconds since logging began (the time_s column).
    double fps = 0.0;      ///< Emitted onto the frame,total row only.
    std::vector<ProfileSampleEntry> entries;
};

/// @brief Pure formatter — one CSV line per entry (no getters, no timing math).
///
/// Column order `time_s,category,name,depth,ms,fps`. `fps` is filled only on
/// the `frame,total` row; `mem` rows carry MB in the `ms` column. Unit-tested
/// headlessly (design §11); `ProfileLog::sample()` does the aggregation.
std::vector<std::string> formatSampleRows(const ProfileSample& sample);

/// @brief Appends throttled (~1 Hz) per-pass CPU/GPU/mem timings to a CSV file.
///
/// The engine calls `sample()` every frame after `PerformanceProfiler::endFrame()`;
/// the class accumulates and writes one averaged row-group per second.
class ProfileLog
{
public:
    ProfileLog() = default;
    ~ProfileLog();

    ProfileLog(const ProfileLog&) = delete;
    ProfileLog& operator=(const ProfileLog&) = delete;

    /// @brief Opens @p path for writing and emits the CSV header. Returns false
    ///        if the file can't be opened (logging stays off).
    bool open(const std::string& path);

    /// @brief Flushes any pending interval and closes the file.
    void close();

    /// @brief Whether the log file is open.
    bool isOpen() const { return m_file != nullptr; }

    /// @brief Accumulate this frame; write an averaged row-group once ~1 s has
    ///        elapsed. @p elapsedSec is cumulative seconds since `open()`.
    void sample(PerformanceProfiler& profiler, double elapsedSec);

private:
    /// @brief One interval accumulator, keyed by (category, name, depth).
    struct Accum
    {
        ProfileCategory category;
        std::string name;
        int depth;
        double sum;
        long count;
    };

    void addSample(ProfileCategory category, const std::string& name, int depth,
                   double value);
    void flush(double elapsedSec);

    std::FILE* m_file = nullptr;
    std::vector<Accum> m_accum;  ///< Interval accumulators, in first-seen order.
    double m_fpsSum = 0.0;
    long m_frameCount = 0;
    double m_lastWriteSec = 0.0;   ///< time_s of the last flush (interval start).
    double m_lastSampleSec = 0.0;  ///< time_s of the last sample (final flush).
    bool m_started = false;        ///< False until the first sample sets origin.

    static constexpr double WRITE_INTERVAL_SEC = 1.0;
};

} // namespace Vestige
