// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_machine_load.cpp
/// @brief The frame log's machine-load readings (3D_E-0968): the parsers of
///        the kernel's text files, and the two "other programs" sums.
#include "profiler/machine_load.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(MachineLoadParse, LoadAverageIsDividedByTheCpuCount)
{
    EXPECT_DOUBLE_EQ(parseLoadAverageShare("6.00 9.21 8.96 2/2422 1210587\n", 12), 0.5);
    EXPECT_DOUBLE_EQ(parseLoadAverageShare("18.00 1.0 1.0 1/1 1\n", 12), 1.5);  // above 1 is legal
    EXPECT_LT(parseLoadAverageShare("", 12), 0.0);
    EXPECT_LT(parseLoadAverageShare("abc", 12), 0.0);
    EXPECT_LT(parseLoadAverageShare("1.00 1.0 1.0 1/1 1\n", 0), 0.0);
}

TEST(MachineLoadParse, ProcStatCountsIdleAndIowaitAsNotBusy)
{
    CpuTicks ticks;
    ASSERT_TRUE(parseProcStatCpu(
        "cpu  100 20 30 400 50 6 7 8 9 10\ncpu0 1 2 3 4 5 6 7 8 9 10\n", ticks));
    EXPECT_EQ(ticks.total, 621u);  // the first eight; guest time is inside user
    EXPECT_EQ(ticks.busy, 171u);   // less idle 400 and iowait 50

    EXPECT_FALSE(parseProcStatCpu("", ticks));
    EXPECT_FALSE(parseProcStatCpu("cpu0 1 2 3 4 5 6 7 8\n", ticks));
    EXPECT_FALSE(parseProcStatCpu("cpu  1 2 3\n", ticks));
}

TEST(MachineLoadParse, SelfStatReadsPastAProgramNameWithSpacesAndBrackets)
{
    unsigned long long ticks = 0;
    ASSERT_TRUE(parseSelfStatTicks(
        "4242 (my game) 2) R 1 4242 4242 0 -1 4194304 500 0 0 0 1234 766 0 0 20 0 9 0 100 1 1\n",
        ticks));
    EXPECT_EQ(ticks, 2000u);  // utime 1234 + stime 766

    EXPECT_FALSE(parseSelfStatTicks("", ticks));
    EXPECT_FALSE(parseSelfStatTicks("4242 (short) R 1 2 3\n", ticks));
}

TEST(MachineLoadParse, GpuClockIsTheStarredLevel)
{
    EXPECT_DOUBLE_EQ(parseGpuClockMhz("0: 500Mhz \n1: 2665Mhz *\n2: 2750Mhz \n"), 2665.0);
    EXPECT_DOUBLE_EQ(parseGpuClockMhz("0: 500Mhz *\n1: 2665Mhz \n"), 500.0);
    EXPECT_LT(parseGpuClockMhz("0: 500Mhz \n1: 2665Mhz \n"), 0.0);
    EXPECT_LT(parseGpuClockMhz(""), 0.0);
}

TEST(MachineLoadParse, GpuBusyPercentIsZeroToAHundred)
{
    EXPECT_DOUBLE_EQ(parseGpuBusyPercent("0\n"), 0.0);
    EXPECT_DOUBLE_EQ(parseGpuBusyPercent("97\n"), 97.0);
    EXPECT_LT(parseGpuBusyPercent("101\n"), 0.0);
    EXPECT_LT(parseGpuBusyPercent(""), 0.0);
    EXPECT_LT(parseGpuBusyPercent("n/a\n"), 0.0);
}

TEST(MachineLoadParse, DrmFdinfoSumsEngineTimesAndSkipsCapacity)
{
    const std::string fdinfo =
        "pos:\t0\nflags:\t02100002\nmnt_id:\t27\nino:\t654\n"
        "drm-driver:\tamdgpu\ndrm-client-id:\t91\ndrm-pdev:\t0000:0a:00.0\n"
        "drm-engine-gfx:\t5000000 ns\ndrm-engine-compute:\t250 ns\n"
        "drm-engine-capacity-gfx:\t1\ndrm-memory-vram:\t1024 KiB\n";
    unsigned long long clientId = 0;
    unsigned long long engineNs = 0;
    ASSERT_TRUE(parseDrmEngineNs(fdinfo, clientId, engineNs));
    EXPECT_EQ(clientId, 91u);
    EXPECT_EQ(engineNs, 5000250u);

    // An ordinary file, and a DRM client that reports no engine time.
    EXPECT_FALSE(parseDrmEngineNs("pos:\t0\nflags:\t02\nmnt_id:\t27\n", clientId, engineNs));
    EXPECT_FALSE(parseDrmEngineNs("drm-driver:\tamdgpu\ndrm-client-id:\t91\n", clientId,
                                  engineNs));
}

// The peak is what OTHER programs used: the run's own time is left out.
TEST(MachineLoadShare, OtherCpuShareLeavesOutOurOwnTicks)
{
    const CpuTicks before{1000, 400};
    const CpuTicks after{2200, 1000};  // 1200 ticks passed, 600 busy
    EXPECT_DOUBLE_EQ(otherCpuShare(before, after, 50, 350), 0.25);  // 300 of the 600 are ours
    EXPECT_DOUBLE_EQ(otherCpuShare(before, after, 50, 50), 0.5);

    // Readings a moment apart can stray outside the range: clamped.
    EXPECT_DOUBLE_EQ(otherCpuShare(before, after, 0, 700), 0.0);
    // No time passed: unknown.
    EXPECT_LT(otherCpuShare(before, before, 0, 0), 0.0);
}

TEST(MachineLoadShare, OtherGpuBusyLeavesOutOurOwnEngineTime)
{
    // GPU 90% busy over one second; we held it for 0.6 s of that.
    EXPECT_DOUBLE_EQ(otherGpuBusyPercent(90.0, 600000000ull, 1000000000ull), 30.0);
    EXPECT_DOUBLE_EQ(otherGpuBusyPercent(90.0, 0, 1000000000ull), 90.0);
    EXPECT_DOUBLE_EQ(otherGpuBusyPercent(50.0, 900000000ull, 1000000000ull), 0.0);  // clamped
    EXPECT_LT(otherGpuBusyPercent(90.0, 1, 0), 0.0);
    EXPECT_LT(otherGpuBusyPercent(-1.0, 1, 1000000000ull), 0.0);
}

// The sampler against the real machine. What it reads depends on the machine,
// so this checks only that the figures it gives are in range and that the
// thread stops.
TEST(MachineLoadSampler, GivesInRangeFiguresAndStops)
{
    MachineLoadSampler sampler;
    sampler.start();
    sampler.beginRun();
    const MachineLoad load = sampler.finish();
    EXPECT_FALSE(sampler.isSampling());

    for (double share : {load.loadPeak})
    {
        EXPECT_TRUE(share < 0.0 || share <= 1.0);
    }
    for (double percent : {load.gpuBusyStart, load.gpuBusyEnd, load.gpuBusyPeak})
    {
        EXPECT_TRUE(percent < 0.0 || percent <= 100.0);
    }
#ifdef __linux__
    EXPECT_GE(load.loadStart, 0.0);
    EXPECT_GE(load.loadEnd, 0.0);
#endif

    const MachineLoad again = sampler.finish();
    EXPECT_DOUBLE_EQ(again.loadEnd, load.loadEnd);
}
