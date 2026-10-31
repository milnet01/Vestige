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
};

/// @brief Builds the header's one-line JSON object. `inputs` is empty: the run
///        does not hash its assets yet, and `frameledger check` warns on that.
std::string frameLedgerHeaderJson(const FrameLedgerRunInfo& info);

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
    double memMb = 0.0;

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
    bool open(const std::string& path, const std::string& runId,
              const std::string& headerJson);

    /// @brief Writes the close line and closes the file. A file without that
    ///        line reads as an unfinished run, which is what a crash leaves.
    void close();

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

    std::FILE* m_file = nullptr;
    std::vector<char> m_buffer;
    std::vector<Row> m_rows;  ///< Reused each frame, so a frame does not allocate.
    fled_writer m_writer{};
    long long m_frame = 0;
    double m_lastFrameStartSec = 0.0;
    bool m_warnedBadRow = false;
};

} // namespace Vestige
