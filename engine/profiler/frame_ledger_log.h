// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file frame_ledger_log.h
/// @brief Per-frame run file in frameledger's shared format (3D_E-0786).
///
/// `ProfileLog` writes one averaged row-group a second, which cannot show a
/// single slow frame. This writes one set of rows per frame, in the format
/// `frameledger check`, `report` and `floor` read: FLED-0001 in the frameledger
/// repository. Off unless a path is opened (CLI: --frame-log=PATH).
#pragma once

#include "profiler/machine_load.h"
#include "profiler/run_inputs.h"

#include <frameledger.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace Vestige
{

class PerformanceProfiler;
struct GpuPassTiming;
struct ProfileEntry;

/// @brief What the run file's header line records about a run.
struct FrameLedgerRunInfo
{
    std::string runId;
    std::string toolVersion;
    std::string buildType;
    std::string commit;
    bool dirty = true;
    std::string device;    ///< GPU name, without the driver details.
    std::string renderer;  ///< GL_RENDERER as reported.
    std::string driver;
    std::string api;
    int width = 0;
    int height = 0;
    double renderScale = 1.0;
    bool vsync = false;
    std::string display;
    std::string scene;
    std::string startedUtc;
    std::string os;
    MachineLoad load;  ///< Known in full only after the last frame.
    std::vector<RunInput> inputs;  ///< Asset files read; known after the last frame.
};

/// @brief Builds the header's one-line JSON object. `inputs` lists
///        `info.inputs`, which is empty until the run has ended (3D_E-0972).
///        `load`, `gpu_busy_pct` and `gpu_clock_mhz` each appear only once both
///        their start and their end are known.
std::string frameLedgerHeaderJson(const FrameLedgerRunInfo& info);

/// @brief Room `FrameLedgerLog::open` keeps after the header for the three
///        load fields and the `inputs` list, which `close` writes once the
///        run has ended. An input group takes about 120 bytes.
constexpr std::size_t FRAME_LEDGER_HEADER_RESERVE = 2048;

/// @brief `GL_RENDERER` without its parenthesised driver details, so a driver
///        update does not read as a different GPU when two runs are compared.
std::string frameLedgerDeviceName(const std::string& glRenderer);

/// @brief The clock readings the engine takes around one frame.
struct FrameLedgerTimes
{
    double frameStartSec = 0.0;  ///< Unclamped clock at this frame's start.
    double cpuSpanMs = 0.0;      ///< Frame start to just before the swap.
    double presentWaitMs = 0.0;  ///< Time inside the swap.
};

/// @brief One frame's data, free of the profiler so tests need no GL context.
struct FrameLedgerFrame
{
    FrameLedgerTimes times;
    const ProfileEntry* cpuEntries = nullptr;
    std::size_t cpuEntryCount = 0;
    double memMb = 0.0;  ///< This process's VRAM in MB; negative = unknown, no row.

    /// GPU results arrive after the frame that submitted them, so they name
    /// their own frame. Negative means this frame delivered none.
    long long gpuFrame = -1;
    const GpuPassTiming* gpuPasses = nullptr;
    std::size_t gpuPassCount = 0;
    double gpuSpanMs = -1.0;  ///< Negative means no span was measured.
};

/// @brief Writes one run file. One thread: the caller's.
class FrameLedgerLog
{
public:
    FrameLedgerLog() = default;
    ~FrameLedgerLog();

    FrameLedgerLog(const FrameLedgerLog&) = delete;
    FrameLedgerLog& operator=(const FrameLedgerLog&) = delete;

    /// @brief Opens @p path and writes the header. Returns false, logging stays
    ///        off, if the file cannot be opened or the header is refused.
    ///        @p headerReserve spaces follow the header's closing brace, which
    ///        the format ignores (FLED-0001 § 4.1), so `close` can write a
    ///        longer header over the line.
    bool open(const std::string& path, const std::string& runId,
              const std::string& headerJson, std::size_t headerReserve = 0);

    /// @brief Writes the close line and closes the file. A file without that
    ///        line reads as an unfinished run, which is what a crash leaves.
    ///        A non-empty @p finalHeaderJson replaces the header written by
    ///        `open`: the load figures are only known now. One that does not
    ///        fit the room `open` kept is dropped with a warning.
    void close(const std::string& finalHeaderJson = {});

    bool isOpen() const { return m_file != nullptr; }

    /// @brief Reads this frame from the profiler and writes it. Call once per
    ///        rendered frame, after `PerformanceProfiler::endFrame()`.
    void sample(PerformanceProfiler& profiler, const FrameLedgerTimes& times);

    /// @brief Writes one frame's rows. A frame's wall time is only known when
    ///        the next one starts, so each call writes the previous frame's,
    ///        and the run's last frame has none, as the format expects.
    void writeFrame(const FrameLedgerFrame& frame);

    /// @brief Frames written so far; the next frame's number.
    long long frameCount() const { return m_frame; }

private:
    /// @brief One row to write, so a name used twice in a frame sums to one row.
    struct Row
    {
        const char* name;
        int depth;
        double ms;
    };

    void addToRows(const char* name, int depth, double ms);
    void writeRows(long long frame, const char* category);
    void writeRow(long long frame, const char* category, const char* name, int depth,
                  double value);
    void writePending();
    void rewriteHeader(const std::string& headerJson);

    std::FILE* m_file = nullptr;
    std::vector<char> m_buffer;
    std::vector<Row> m_rows;  ///< Reused each frame, so a frame does not allocate.
    fled_writer m_writer{};
    long long m_frame = 0;
    std::size_t m_headerRoom = 0;  ///< Bytes of header JSON plus its padding.
    double m_lastFrameStartSec = 0.0;
    bool m_warnedBadRow = false;
};

} // namespace Vestige
