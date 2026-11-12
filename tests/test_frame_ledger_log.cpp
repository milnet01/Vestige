// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_frame_ledger_log.cpp
/// @brief The per-frame run file: which rows land on which frame (3D_E-0786).

#include <gtest/gtest.h>

#include "profiler/cpu_profiler.h"
#include "profiler/frame_ledger_log.h"
#include "profiler/gpu_timer.h"
#include "test_helpers.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{

class FrameLedgerLogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_path = (std::filesystem::temp_directory_path()
                  / ("vestige_frame_ledger_" + Testing::vestigeTestStamp() + ".csv"))
                     .string();
        ASSERT_TRUE(m_log.open(m_path, "run", "{\"schema\":1}"));
    }

    void TearDown() override
    {
        m_log.close();
        std::filesystem::remove(m_path);
    }

    /// Closes the log and returns the file's text.
    std::string finish()
    {
        m_log.close();
        std::ifstream in(m_path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    static FrameLedgerFrame frameAt(double startSec)
    {
        FrameLedgerFrame frame;
        frame.times.frameStartSec = startSec;
        frame.times.cpuSpanMs = 4.0;
        frame.times.presentWaitMs = 1.0;
        return frame;
    }

    std::string m_path;
    FrameLedgerLog m_log;
};

bool contains(const std::string& text, const std::string& line)
{
    return text.find("\n" + line + "\n") != std::string::npos;
}

} // namespace

TEST_F(FrameLedgerLogTest, FileHasTheFormatsHeaderColumnsAndCloseLine)
{
    m_log.writeFrame(frameAt(0.0));
    const std::string text = finish();
    EXPECT_EQ(text.rfind("# frameledger {\"schema\":1}\n"
                         "run_id,segment,frame,category,name,depth,value\n", 0), 0u);
    EXPECT_TRUE(contains(text, "# frameledger-close {\"rows_dropped\":0,\"frames\":1}"));
}

// The wall time is start to next start, so it lands on the earlier frame and
// the last frame has none. An unclamped clock lets a 400 ms hitch read as 400.
TEST_F(FrameLedgerLogTest, WallTimeLandsOnTheEarlierFrameAndLastFrameHasNone)
{
    m_log.writeFrame(frameAt(1.0));
    m_log.writeFrame(frameAt(1.4));
    m_log.writeFrame(frameAt(1.41));
    const std::string text = finish();
    EXPECT_TRUE(contains(text, "run,,0,frame,wall,0,400.000"));
    EXPECT_TRUE(contains(text, "run,,1,frame,wall,0,10.000"));
    EXPECT_EQ(text.find(",2,frame,wall,"), std::string::npos);
}

TEST_F(FrameLedgerLogTest, CpuTimesAndMemoryLandOnTheirOwnFrame)
{
    const ProfileEntry scopes[] = {{"SceneUpdate", 1.0f, 3.5f, -1, 0},
                                  {"Culling", 1.0f, 2.0f, 0, 1}};
    FrameLedgerFrame frame = frameAt(0.0);
    frame.cpuEntries = scopes;
    frame.cpuEntryCount = 2;
    frame.memMb = 512.0;
    m_log.writeFrame(frame);
    const std::string text = finish();
    EXPECT_TRUE(contains(text, "run,,0,frame,cpu_span,0,4.000"));
    EXPECT_TRUE(contains(text, "run,,0,frame,present_wait,0,1.000"));
    EXPECT_TRUE(contains(text, "run,,0,cpu,SceneUpdate,0,2.500"));
    EXPECT_TRUE(contains(text, "run,,0,cpu,Culling,1,1.000"));
    EXPECT_TRUE(contains(text, "run,,0,mem,gpu_mb,0,512.000"));
}

// GPU results come back after the frame that submitted them; the format counts
// them against the submitting frame.
TEST_F(FrameLedgerLogTest, GpuRowsLandOnTheFrameThatSubmittedThem)
{
    m_log.writeFrame(frameAt(0.0));
    m_log.writeFrame(frameAt(0.01));
    const std::vector<GpuPassTiming> passes = {{"Scene", 6.0f}};
    FrameLedgerFrame frame = frameAt(0.02);
    frame.gpuFrame = 0;
    frame.gpuPasses = passes.data();
    frame.gpuPassCount = passes.size();
    frame.gpuSpanMs = 7.5;
    m_log.writeFrame(frame);
    const std::string text = finish();
    EXPECT_TRUE(contains(text, "run,,0,gpu,Scene,0,6.000"));
    EXPECT_TRUE(contains(text, "run,,0,frame,gpu_span,0,7.500"));
    EXPECT_EQ(text.find(",2,gpu,"), std::string::npos);
}

TEST_F(FrameLedgerLogTest, NoGpuResultWritesNoGpuRows)
{
    m_log.writeFrame(frameAt(0.0));
    const std::string text = finish();
    EXPECT_EQ(text.find(",gpu,"), std::string::npos);
    EXPECT_EQ(text.find("gpu_span"), std::string::npos);
}

// A pass or scope that runs twice in a frame costs the sum, on one row.
TEST_F(FrameLedgerLogTest, ANameUsedTwiceInAFrameSumsToOneRow)
{
    const ProfileEntry scopes[] = {{"Water", 0.0f, 1.0f, -1, 0},
                                  {"Water", 2.0f, 4.0f, -1, 0}};
    FrameLedgerFrame frame = frameAt(0.0);
    frame.cpuEntries = scopes;
    frame.cpuEntryCount = 2;
    m_log.writeFrame(frame);
    const std::string text = finish();
    EXPECT_TRUE(contains(text, "run,,0,cpu,Water,0,3.000"));
}

// More rows than the buffer holds must reach the file, none dropped.
TEST_F(FrameLedgerLogTest, ALongRunDropsNoRows)
{
    const int frames = 40000;
    for (int i = 0; i < frames; ++i)
    {
        m_log.writeFrame(frameAt(i * 0.01));
    }
    const std::string text = finish();
    EXPECT_GT(text.size(), 1024u * 1024u);
    EXPECT_TRUE(contains(text, "# frameledger-close {\"rows_dropped\":0,\"frames\":40000}"));
    EXPECT_TRUE(contains(text, "run,,39998,frame,wall,0,10.000"));
}

TEST(FrameLedgerLogOpen, AnUnwritablePathLeavesLoggingOff)
{
    FrameLedgerLog log;
    EXPECT_FALSE(log.open("/nonexistent_dir_vestige/run.csv", "run", "{\"schema\":1}"));
    EXPECT_FALSE(log.isOpen());
}

TEST(FrameLedgerLogOpen, AnEmptyRunIdIsRefused)
{
    FrameLedgerLog log;
    EXPECT_FALSE(log.open("unused.csv", "", "{\"schema\":1}"));
    EXPECT_FALSE(log.isOpen());
    EXPECT_FALSE(std::filesystem::exists("unused.csv"));
}

TEST(FrameLedgerHeader, IsOneLineOfJsonWithEveryRequiredField)
{
    FrameLedgerRunInfo info;
    info.runId = "r";
    info.renderer = "GPU \"X\"\nnext line";
    info.width = 1280;
    info.height = 720;
    const std::string text = frameLedgerHeaderJson(info);
    EXPECT_EQ(text.find('\n'), std::string::npos);

    const nlohmann::json header = nlohmann::json::parse(text);
    for (const char* field :
         {"schema", "run_id", "engine", "tool", "build_type", "commit", "dirty", "device",
          "renderer", "driver", "api", "resolution", "render_scale", "vsync", "pipelined",
          "display", "scene", "inputs", "started_utc"})
    {
        EXPECT_TRUE(header.contains(field)) << field;
    }
    EXPECT_EQ(header["renderer"], "GPU \"X\"\nnext line");
    EXPECT_EQ(header["resolution"][1], 720);
}

// 3D_E-0968: the load fields need both ends of the run. Until then the header
// leaves them out, which the format reads as unknown.
TEST(FrameLedgerHeader, LoadFieldsAppearOnlyOnceStartAndEndAreKnown)
{
    FrameLedgerRunInfo info;
    info.runId = "r";
    info.load.loadStart = 0.25;
    info.load.gpuBusyStart = 3.0;
    info.load.gpuClockStartMhz = 500.0;
    nlohmann::json header = nlohmann::json::parse(frameLedgerHeaderJson(info));
    EXPECT_FALSE(header.contains("load"));
    EXPECT_FALSE(header.contains("gpu_busy_pct"));
    EXPECT_FALSE(header.contains("gpu_clock_mhz"));

    info.load.loadEnd = 0.31234;
    info.load.gpuBusyEnd = 97.0;
    info.load.gpuClockEndMhz = 2665.0;
    header = nlohmann::json::parse(frameLedgerHeaderJson(info));
    EXPECT_EQ(header["load"], (nlohmann::json{{"start", 0.25}, {"end", 0.312}}));
    EXPECT_EQ(header["gpu_busy_pct"], (nlohmann::json{{"start", 3.0}, {"end", 97.0}}));
    EXPECT_EQ(header["gpu_clock_mhz"], (nlohmann::json{{"start", 500.0}, {"end", 2665.0}}));

    info.load.loadPeak = 0.0;
    info.load.gpuBusyPeak = 21.5;
    header = nlohmann::json::parse(frameLedgerHeaderJson(info));
    EXPECT_EQ(header["load"]["peak"], 0.0);
    EXPECT_EQ(header["gpu_busy_pct"]["peak"], 21.5);
    EXPECT_FALSE(header["gpu_clock_mhz"].contains("peak"));
}

// The fullest header the engine can write must fit the room open() keeps.
TEST(FrameLedgerHeader, LoadFieldsFitTheReservedRoom)
{
    FrameLedgerRunInfo info;
    info.runId = "r";
    const std::size_t without = frameLedgerHeaderJson(info).size();
    info.load.loadStart = 123.456;
    info.load.loadEnd = 123.456;
    info.load.loadPeak = 0.123;
    info.load.gpuBusyStart = 100.0;
    info.load.gpuBusyEnd = 100.0;
    info.load.gpuBusyPeak = 99.999;
    info.load.gpuClockStartMhz = 12345.0;
    info.load.gpuClockEndMhz = 12345.0;
    EXPECT_LE(frameLedgerHeaderJson(info).size() - without, FRAME_LEDGER_HEADER_RESERVE);
}

// 3D_E-0968: close() writes the finished header over the padded one, and
// every byte after line 1 stays where it was.
TEST(FrameLedgerLogOpen, CloseReplacesThePaddedHeaderInPlace)
{
    const std::string path = (std::filesystem::temp_directory_path()
                              / ("vestige_frame_ledger_pad_" + Testing::vestigeTestStamp()
                                 + ".csv"))
                                 .string();
    auto readAll = [&path]() {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    };

    FrameLedgerFrame frame;
    frame.times.cpuSpanMs = 4.0;

    FrameLedgerLog plain;
    ASSERT_TRUE(plain.open(path, "run", "{\"schema\":1}", 40));
    plain.writeFrame(frame);
    plain.close();
    const std::string before = readAll();
    const std::size_t lineEnd = before.find('\n');
    ASSERT_NE(lineEnd, std::string::npos);
    EXPECT_EQ(before.substr(0, lineEnd), "# frameledger {\"schema\":1}" + std::string(40, ' '));

    FrameLedgerLog finished;
    ASSERT_TRUE(finished.open(path, "run", "{\"schema\":1}", 40));
    finished.writeFrame(frame);
    finished.close("{\"schema\":1,\"load\":{\"start\":0.1,\"end\":0.2}}");
    const std::string after = readAll();

    const std::string json = "{\"schema\":1,\"load\":{\"start\":0.1,\"end\":0.2}}";
    ASSERT_EQ(after.size(), before.size());
    EXPECT_EQ(after.substr(0, lineEnd),
              "# frameledger " + json + std::string(before.substr(0, lineEnd).size() - 14 - json.size(), ' '));
    EXPECT_EQ(after.substr(lineEnd), before.substr(lineEnd));

    // A header too long for the room is dropped; the file stays as open() left it.
    FrameLedgerLog tooLong;
    ASSERT_TRUE(tooLong.open(path, "run", "{\"schema\":1}", 4));
    tooLong.writeFrame(frame);
    tooLong.close("{\"schema\":1,\"load\":{\"start\":0.1,\"end\":0.2}}");
    const std::string kept = readAll();
    EXPECT_EQ(kept.substr(0, kept.find('\n')), "# frameledger {\"schema\":1}    ");
    EXPECT_NE(kept.find("# frameledger-close"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(FrameLedgerHeader, DeviceNameDropsTheDriverDetails)
{
    EXPECT_EQ(frameLedgerDeviceName("AMD Radeon RX 6600 (radeonsi, navi23, LLVM 21)"),
              "AMD Radeon RX 6600");
    EXPECT_EQ(frameLedgerDeviceName("NVIDIA GeForce GTX 1050/PCIe/SSE2"),
              "NVIDIA GeForce GTX 1050/PCIe/SSE2");
}
