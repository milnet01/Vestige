// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file frame_ledger_log.cpp
/// @brief FrameLedgerLog implementation (3D_E-0786).

#include "profiler/frame_ledger_log.h"

#include "core/logger.h"
#include "profiler/performance_profiler.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>

namespace Vestige
{

namespace
{

/// Room for several seconds of frames. A frame is far below the half this
/// keeps free, so writing the file when half full never drops a row.
constexpr std::size_t BUFFER_BYTES = 1024 * 1024;

/// What frameledger.h's fled_open writes before the header's JSON.
constexpr char HEADER_PREFIX[] = "# frameledger ";

/// Adds `{"start": s, "end": e}` and, if known, `"peak"`. The format wants a
/// start and an end together, so one without the other adds nothing. Three
/// decimals keep the line short; the figures are not that exact anyway.
void addStartEnd(nlohmann::json& header, const char* field, double start, double end,
                 double peak)
{
    if (start < 0.0 || end < 0.0)
    {
        return;
    }
    auto rounded = [](double value) { return std::round(value * 1000.0) / 1000.0; };
    header[field] = {{"start", rounded(start)}, {"end", rounded(end)}};
    if (peak >= 0.0)
    {
        header[field]["peak"] = rounded(peak);
    }
}

} // namespace

std::string frameLedgerHeaderJson(const FrameLedgerRunInfo& info)
{
    nlohmann::json header = {
        {"schema", FLED_SCHEMA},
        {"run_id", info.runId},
        {"engine", "vestige"},
        {"tool", {{"name", "vestige"}, {"version", info.toolVersion}}},
        {"build_type", info.buildType},
        {"commit", info.commit},
        {"dirty", info.dirty},
        {"device", info.device},
        {"renderer", info.renderer},
        {"driver", info.driver},
        {"api", info.api},
        {"resolution", {info.width, info.height}},
        {"render_scale", info.renderScale},
        {"vsync", info.vsync},
        {"pipelined", false},
        {"display", info.display},
        {"scene", info.scene},
        {"inputs", nlohmann::json::array()},
        {"started_utc", info.startedUtc},
        {"os", info.os},
    };
    const MachineLoad& load = info.load;
    addStartEnd(header, "load", load.loadStart, load.loadEnd, load.loadPeak);
    addStartEnd(header, "gpu_busy_pct", load.gpuBusyStart, load.gpuBusyEnd, load.gpuBusyPeak);
    addStartEnd(header, "gpu_clock_mhz", load.gpuClockStartMhz, load.gpuClockEndMhz, -1.0);
    // A driver string is outside our control: replace bad UTF-8, never throw.
    return header.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string frameLedgerDeviceName(const std::string& glRenderer)
{
    const std::size_t details = glRenderer.find(" (");
    return details == std::string::npos ? glRenderer : glRenderer.substr(0, details);
}

FrameLedgerLog::~FrameLedgerLog()
{
    close();
}

bool FrameLedgerLog::open(const std::string& path, const std::string& runId,
                          const std::string& headerJson, std::size_t headerReserve)
{
    close();
    m_buffer.resize(BUFFER_BYTES);
    const std::string padded = headerJson + std::string(headerReserve, ' ');
    m_headerRoom = padded.size();
    if (fled_open(&m_writer, m_buffer.data(), m_buffer.size(), runId.c_str(),
                  padded.c_str(), nullptr) != FLED_OK)
    {
        Logger::warning("Frame log: the run header was refused");
        return false;
    }
    m_file = std::fopen(path.c_str(), "wb");
    if (!m_file)
    {
        return false;
    }
    m_frame = 0;
    m_lastFrameStartSec = 0.0;
    m_warnedBadRow = false;
    writePending();
    return m_file != nullptr;
}

void FrameLedgerLog::close(const std::string& finalHeaderJson)
{
    if (!m_file)
    {
        return;
    }
    fled_close(&m_writer, m_frame);
    writePending();
    if (m_file && !finalHeaderJson.empty())
    {
        rewriteHeader(finalHeaderJson);
    }
    if (m_file)
    {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

void FrameLedgerLog::rewriteHeader(const std::string& headerJson)
{
    // Same length as the line open() wrote, so nothing after it moves.
    if (headerJson.size() > m_headerRoom
        || headerJson.find_first_of("\r\n") != std::string::npos)
    {
        Logger::warning("Frame log: the finished header does not fit; the load fields are "
                        "left out");
        return;
    }
    const std::string line = headerJson + std::string(m_headerRoom - headerJson.size(), ' ');
    if (std::fseek(m_file, static_cast<long>(sizeof(HEADER_PREFIX) - 1), SEEK_SET) != 0
        || std::fwrite(line.data(), 1, line.size(), m_file) != line.size()
        || std::fflush(m_file) != 0)
    {
        Logger::warning("Frame log: rewriting the header failed");
    }
}

void FrameLedgerLog::sample(PerformanceProfiler& profiler, const FrameLedgerTimes& times)
{
    if (!m_file)
    {
        return;
    }
    const std::vector<ProfileEntry>& cpu = profiler.getCpuProfiler().getLastFrame();
    GpuTimer& gpu = profiler.getGpuTimer();

    FrameLedgerFrame frame;
    frame.times = times;
    frame.cpuEntries = cpu.data();
    frame.cpuEntryCount = cpu.size();
    const MemoryTracker& mem = profiler.getMemoryTracker();
    frame.memMb = mem.hasGpuUsed() ? static_cast<double>(mem.getGpuUsedMB()) : -1.0;
    if (gpu.hasResults() && gpu.hasFreshResults() && m_frame >= GpuTimer::RESULT_LATENCY_FRAMES)
    {
        frame.gpuFrame = m_frame - GpuTimer::RESULT_LATENCY_FRAMES;
        frame.gpuPasses = gpu.getResults().data();
        frame.gpuPassCount = gpu.getResults().size();
        if (gpu.hasFrameSpan())
        {
            frame.gpuSpanMs = gpu.getFrameSpanMs();
        }
    }
    writeFrame(frame);
}

void FrameLedgerLog::writeFrame(const FrameLedgerFrame& frame)
{
    if (!m_file)
    {
        return;
    }
    const FrameLedgerTimes& times = frame.times;
    if (m_frame > 0)
    {
        writeRow(m_frame - 1, "frame", "wall", 0,
                 (times.frameStartSec - m_lastFrameStartSec) * 1000.0);
    }
    m_lastFrameStartSec = times.frameStartSec;

    writeRow(m_frame, "frame", "cpu_span", 0, times.cpuSpanMs);
    writeRow(m_frame, "frame", "present_wait", 0, times.presentWaitMs);
    if (frame.memMb >= 0.0)
    {
        writeRow(m_frame, "mem", "gpu_mb", 0, frame.memMb);
    }

    m_rows.clear();
    for (std::size_t i = 0; i < frame.cpuEntryCount; ++i)
    {
        const ProfileEntry& e = frame.cpuEntries[i];
        addToRows(e.name ? e.name : "unnamed", e.depth,
                  static_cast<double>(e.endMs - e.startMs));
    }
    writeRows(m_frame, "cpu");

    if (frame.gpuFrame >= 0)
    {
        if (frame.gpuSpanMs >= 0.0)
        {
            writeRow(frame.gpuFrame, "frame", "gpu_span", 0, frame.gpuSpanMs);
        }
        m_rows.clear();
        for (std::size_t i = 0; i < frame.gpuPassCount; ++i)
        {
            const GpuPassTiming& p = frame.gpuPasses[i];
            addToRows(p.name.c_str(), 0, static_cast<double>(p.timeMs));
        }
        writeRows(frame.gpuFrame, "gpu");
    }

    ++m_frame;

    if (fled_pending(&m_writer, nullptr) >= m_buffer.size() / 2)
    {
        writePending();
    }
}

void FrameLedgerLog::addToRows(const char* name, int depth, double ms)
{
    // A frame holds a few dozen names, so a scan beats a map and allocates nothing.
    for (Row& row : m_rows)
    {
        if (row.depth == depth && std::strcmp(row.name, name) == 0)
        {
            row.ms += ms;
            return;
        }
    }
    m_rows.push_back({name, depth, ms});
}

void FrameLedgerLog::writeRows(long long frame, const char* category)
{
    for (const Row& row : m_rows)
    {
        writeRow(frame, category, row.name, row.depth, row.ms);
    }
}

void FrameLedgerLog::writeRow(long long frame, const char* category, const char* name,
                              int depth, double value)
{
    if (fled_row(&m_writer, nullptr, frame, category, name, depth, value) == FLED_BAD_ARG
        && !m_warnedBadRow)
    {
        m_warnedBadRow = true;
        Logger::warning(std::string("Frame log: refused a row named '") + name
                        + "'; names allow letters, digits and _ . / -");
    }
}

void FrameLedgerLog::writePending()
{
    const char* bytes = nullptr;
    const std::size_t pending = fled_pending(&m_writer, &bytes);
    if (std::fwrite(bytes, 1, pending, m_file) != pending || std::fflush(m_file) != 0)
    {
        // Rows are already lost. Stop here, leaving no close line, so the file
        // reads as the unfinished run it is.
        Logger::warning("Frame log: writing failed; logging stopped");
        std::fclose(m_file);
        m_file = nullptr;
        return;
    }
    fled_consume(&m_writer, pending);
}

} // namespace Vestige
