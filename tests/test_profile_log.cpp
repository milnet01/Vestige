// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_profile_log.cpp
/// @brief Headless unit tests for the profiler CSV formatter (design §8.1/§11).
///        `formatSampleRows` is a pure function — no GL context, no getters — so
///        the row format is pinned independently of a running engine.

#include <gtest/gtest.h>

#include "profiler/performance_profiler.h"
#include "profiler/profile_log.h"
#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{

// A representative sample mirroring the §8.1 example: one synthetic `total` per
// category plus real per-pass/per-scope rows, a nested CPU scope, and a mem row.
ProfileSample makeSample()
{
    ProfileSample s;
    s.timeSec = 12.0;
    s.fps = 62.9;
    s.entries = {
        {ProfileCategory::Frame, "total", 0, 15.9},
        {ProfileCategory::Gpu, "total", 0, 9.8},
        {ProfileCategory::Gpu, "ShadowPass", 0, 2.1},
        {ProfileCategory::Cpu, "total", 0, 4.2},
        {ProfileCategory::Cpu, "SceneUpdate", 0, 2.6},
        {ProfileCategory::Cpu, "Culling", 1, 0.8},
        {ProfileCategory::Mem, "gpu_mb", 0, 512.0},
    };
    return s;
}

} // namespace

TEST(ProfileLog, RowPerEntryInOrder)
{
    const auto rows = formatSampleRows(makeSample());
    ASSERT_EQ(rows.size(), 7u);
    // First-seen order is preserved (frame, gpu…, cpu…, mem).
    EXPECT_EQ(rows[0], "12.00,frame,total,0,15.900,62.9");
    EXPECT_EQ(rows[6], "12.00,mem,gpu_mb,0,512,");
}

TEST(ProfileLog, FpsOnlyOnFrameTotalRow)
{
    const auto rows = formatSampleRows(makeSample());
    // frame,total carries fps in the last column.
    EXPECT_EQ(rows[0], "12.00,frame,total,0,15.900,62.9");
    // Every other row ends with an empty fps column (a trailing comma).
    for (size_t i = 1; i < rows.size(); ++i)
    {
        EXPECT_EQ(rows[i].back(), ',') << "row " << i << ": " << rows[i];
    }
    // The gpu `total` row must NOT get fps even though its name is "total".
    EXPECT_EQ(rows[1], "12.00,gpu,total,0,9.800,");
}

TEST(ProfileLog, CpuDepthIsCarriedThrough)
{
    const auto rows = formatSampleRows(makeSample());
    // SceneUpdate is depth 0, its child Culling is depth 1.
    EXPECT_EQ(rows[4], "12.00,cpu,SceneUpdate,0,2.600,");
    EXPECT_EQ(rows[5], "12.00,cpu,Culling,1,0.800,");
}

TEST(ProfileLog, MemValueIsIntegerMbInMsColumn)
{
    const auto rows = formatSampleRows(makeSample());
    // mem carries MB as an integer in the `ms` column (no decimals), fps blank.
    EXPECT_EQ(rows[6], "12.00,mem,gpu_mb,0,512,");
}

TEST(ProfileLog, EmptySampleYieldsNoRows)
{
    ProfileSample s;
    s.timeSec = 3.0;
    s.fps = 60.0;
    EXPECT_TRUE(formatSampleRows(s).empty());
}

// 3D_E-0800: the fps column is frames over wall time, not a mean of per-frame
// rates. 50 frames at 10 ms plus one 500 ms frame is 51 frames in one second;
// a mean of per-frame FPS read about 98 and passed tools/fps_floor.py.
TEST(ProfileLog, FpsIsFramesOverWallTimeSoAHitchShows)
{
    const std::string path = (std::filesystem::temp_directory_path()
        / ("vestige_profile_log_" + Testing::vestigeTestStamp() + ".csv")).string();
    PerformanceProfiler profiler;
    ProfileLog log;
    ASSERT_TRUE(log.open(path));

    auto frame = [&](float frameSec, double elapsedSec)
    {
        profiler.endFrame(frameSec);
        log.sample(profiler, elapsedSec);
    };

    frame(0.010f, 0.0);  // the first sample only anchors the interval
    for (int i = 1; i <= 50; ++i)
    {
        frame(0.010f, i * 0.010);
    }
    frame(0.500f, 1.0);  // the hitch; ends the first second
    for (int i = 1; i <= 60; ++i)
    {
        frame(1.0f / 60.0f, 1.0 + (i / 60.0));  // a steady second at 60
    }
    log.close();

    std::vector<std::string> fps;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
    {
        if (line.find(",frame,total,") != std::string::npos)
        {
            fps.push_back(line.substr(line.rfind(',') + 1));
        }
    }
    in.close();
    std::filesystem::remove(path);

    ASSERT_EQ(fps.size(), 2u);
    EXPECT_EQ(fps[0], "51.0") << "a hitch second must read its real rate";
    EXPECT_EQ(fps[1], "60.0");
}

// 3D_E-0786: the frame,total ms is the mean of the interval's own frame times,
// counted over the same frames as fps, not an average of the profiler's
// 300-frame rolling average. 50 frames at 10 ms plus one 500 ms frame is
// 1000 ms over 51 frames: 19.608 ms, the same second fps reads as 51.
TEST(ProfileLog, FrameMsIsTheIntervalsOwnFramesSoAHitchShows)
{
    const std::string path = (std::filesystem::temp_directory_path()
        / ("vestige_profile_log_" + Testing::vestigeTestStamp() + ".csv")).string();
    PerformanceProfiler profiler;
    ProfileLog log;
    ASSERT_TRUE(log.open(path));

    auto frame = [&](float frameSec, double elapsedSec)
    {
        profiler.endFrame(frameSec);
        log.sample(profiler, elapsedSec);
    };

    frame(0.250f, 0.0);  // the anchor: its time belongs to no interval
    for (int i = 1; i <= 50; ++i)
    {
        frame(0.010f, i * 0.010);
    }
    frame(0.500f, 1.0);  // the hitch; ends the first second
    log.close();

    std::vector<std::string> ms;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
    {
        const auto at = line.find(",frame,total,0,");
        if (at != std::string::npos)
        {
            const auto start = at + std::string(",frame,total,0,").size();
            ms.push_back(line.substr(start, line.rfind(',') - start));
        }
    }
    in.close();
    std::filesystem::remove(path);

    ASSERT_EQ(ms.size(), 1u);
    EXPECT_EQ(ms[0], "19.608");
}
