// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file profile_log.cpp
/// @brief Throttled profiler → CSV logger (meadow design §8.1).

#include "profiler/profile_log.h"

#include "profiler/performance_profiler.h"

#include <cstdio>

namespace Vestige
{

namespace
{

const char* categoryStr(ProfileCategory c)
{
    switch (c)
    {
        case ProfileCategory::Frame: return "frame";
        case ProfileCategory::Gpu:   return "gpu";
        case ProfileCategory::Cpu:   return "cpu";
        case ProfileCategory::Mem:   return "mem";
    }
    return "?";
}

/// @brief snprintf a double into a std::string at fixed precision (no locale
///        surprises, no stream setup).
std::string fixed(double v, int precision)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
    return buf;
}

} // namespace

std::vector<std::string> formatSampleRows(const ProfileSample& sample)
{
    std::vector<std::string> rows;
    rows.reserve(sample.entries.size());

    const std::string timeCol = fixed(sample.timeSec, 2);

    for (const auto& e : sample.entries)
    {
        const bool isFrameTotal =
            e.category == ProfileCategory::Frame && e.name == "total";

        // The `ms` column holds MB (integer) for mem rows, milliseconds (3 dp)
        // otherwise — disambiguated by `category`, kept in one column so the
        // long format stays simple (§8.1).
        const std::string valCol =
            (e.category == ProfileCategory::Mem) ? fixed(e.value, 0)
                                                 : fixed(e.value, 3);

        // fps only on the frame,total row; blank elsewhere.
        const std::string fpsCol = isFrameTotal ? fixed(sample.fps, 1) : "";

        std::string row = timeCol;
        row += ',';
        row += categoryStr(e.category);
        row += ',';
        row += e.name;
        row += ',';
        row += std::to_string(e.depth);
        row += ',';
        row += valCol;
        row += ',';
        row += fpsCol;
        rows.push_back(std::move(row));
    }
    return rows;
}

ProfileLog::~ProfileLog()
{
    close();
}

bool ProfileLog::open(const std::string& path)
{
    if (m_file)
    {
        close();
    }
    m_file = std::fopen(path.c_str(), "w");
    if (!m_file)
    {
        return false;
    }
    std::fputs("time_s,category,name,depth,ms,fps\n", m_file);

    m_accum.clear();
    m_fpsSum = 0.0;
    m_frameCount = 0;
    m_lastWriteSec = 0.0;
    m_lastSampleSec = 0.0;
    m_started = false;
    return true;
}

void ProfileLog::close()
{
    if (!m_file)
    {
        return;
    }
    // Don't lose the final partial (<1 s) interval on a short run.
    if (m_frameCount > 0)
    {
        flush(m_lastSampleSec);
    }
    std::fclose(m_file);
    m_file = nullptr;
}

void ProfileLog::addSample(ProfileCategory category, const std::string& name,
                           int depth, double value)
{
    // Pass sets are tiny (≤16 GPU passes + a handful of CPU scopes), so a linear
    // scan is cheaper than a map and preserves first-seen order → deterministic,
    // readable CSV rows.
    for (auto& a : m_accum)
    {
        if (a.category == category && a.depth == depth && a.name == name)
        {
            a.sum += value;
            ++a.count;
            return;
        }
    }
    m_accum.push_back({category, name, depth, value, 1});
}

void ProfileLog::sample(PerformanceProfiler& profiler, double elapsedSec)
{
    if (!m_file)
    {
        return;
    }
    if (!m_started)
    {
        m_lastWriteSec = elapsedSec;  // anchor the first interval to first sample
        m_started = true;
    }
    m_lastSampleSec = elapsedSec;

    // Frame total. getAvgFrameTimeMs() is itself a windowed average; averaging
    // it again over the interval is a harmless no-op that keeps sample() uniform.
    // (Profiler getters are float; widen explicitly for the double accumulators.)
    addSample(ProfileCategory::Frame, "total", 0,
              static_cast<double>(profiler.getAvgFrameTimeMs()));
    m_fpsSum += static_cast<double>(profiler.getFps());

    // GPU — only once the triple-buffered timestamp queries are warm.
    GpuTimer& gpu = profiler.getGpuTimer();
    if (gpu.hasResults())
    {
        addSample(ProfileCategory::Gpu, "total", 0,
                  static_cast<double>(gpu.getTotalGpuTimeMs()));
        for (const auto& p : gpu.getResults())
        {
            addSample(ProfileCategory::Gpu, p.name, 0,
                      static_cast<double>(p.timeMs));
        }
    }

    // CPU — hierarchical; scope duration is endMs − startMs (there is no single
    // ms field; §3.6), depth carried through so nesting stays visible.
    CpuProfiler& cpu = profiler.getCpuProfiler();
    addSample(ProfileCategory::Cpu, "total", 0,
              static_cast<double>(cpu.getTotalCpuTimeMs()));
    for (const auto& e : cpu.getLastFrame())
    {
        addSample(ProfileCategory::Cpu, e.name ? e.name : "?", e.depth,
                  static_cast<double>(e.endMs - e.startMs));
    }

    // Memory (MB).
    addSample(ProfileCategory::Mem, "gpu_mb", 0,
              static_cast<double>(profiler.getMemoryTracker().getGpuUsedMB()));

    ++m_frameCount;

    if (elapsedSec - m_lastWriteSec >= WRITE_INTERVAL_SEC)
    {
        flush(elapsedSec);
    }
}

void ProfileLog::flush(double elapsedSec)
{
    if (!m_file || m_frameCount == 0)
    {
        return;
    }

    ProfileSample s;
    s.timeSec = elapsedSec;
    s.fps = m_fpsSum / static_cast<double>(m_frameCount);
    s.entries.reserve(m_accum.size());
    for (const auto& a : m_accum)
    {
        s.entries.push_back(
            {a.category, a.name, a.depth, a.sum / static_cast<double>(a.count)});
    }

    for (const auto& row : formatSampleRows(s))
    {
        std::fputs(row.c_str(), m_file);
        std::fputc('\n', m_file);
    }
    std::fflush(m_file);

    m_accum.clear();
    m_fpsSum = 0.0;
    m_frameCount = 0;
    m_lastWriteSec = elapsedSec;
}

} // namespace Vestige
