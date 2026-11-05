// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_performance_profiler.cpp
/// @brief PerformanceProfiler's frame-time statistics (3D_E-0792). No GL:
///        the GPU timer is never initialised, so its calls do nothing.

#include "profiler/performance_profiler.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(PerformanceProfiler, StatsCoverTheFramesSeen)
{
    PerformanceProfiler profiler;
    profiler.endFrame(0.010f);
    profiler.endFrame(0.020f);
    profiler.endFrame(0.030f);

    EXPECT_FLOAT_EQ(profiler.getFrameTimeMs(), 30.0f);
    EXPECT_NEAR(profiler.getFps(), 1000.0f / 30.0f, 1e-3f);
    EXPECT_FLOAT_EQ(profiler.getAvgFrameTimeMs(), 20.0f)
        << "empty history slots must not drag the average down";
    EXPECT_FLOAT_EQ(profiler.getMinFrameTimeMs(), 10.0f);
    EXPECT_FLOAT_EQ(profiler.getMaxFrameTimeMs(), 30.0f);
}

TEST(PerformanceProfiler, HistoryKeepsOnlyTheLastWindow)
{
    PerformanceProfiler profiler;
    profiler.endFrame(0.500f);  // a hitch that later frames push out
    for (int i = 0; i < PerformanceProfiler::HISTORY_SIZE; ++i)
    {
        profiler.endFrame(0.016f);
    }
    EXPECT_FLOAT_EQ(profiler.getMaxFrameTimeMs(), 16.0f);
    EXPECT_FLOAT_EQ(profiler.getAvgFrameTimeMs(), 16.0f);
}

// lane-23 lead performance_panel.cpp:25: the closed panel calls
// setEnabled(false) every frame; a log's request must survive it.
TEST(PerformanceProfiler, KeptOnSurvivesTheClosedPanel)
{
    PerformanceProfiler profiler;
    profiler.setKeptOn(true);
    profiler.setEnabled(false);
    EXPECT_TRUE(profiler.isEnabled());
    profiler.setKeptOn(false);
    EXPECT_FALSE(profiler.isEnabled());
    profiler.setEnabled(true);
    EXPECT_TRUE(profiler.isEnabled());
}

TEST(PerformanceProfiler, NoFrameYetReadsZero)
{
    PerformanceProfiler profiler;
    EXPECT_FLOAT_EQ(profiler.getFps(), 0.0f);
    EXPECT_FLOAT_EQ(profiler.getAvgFrameTimeMs(), 0.0f);
    EXPECT_FLOAT_EQ(profiler.getMaxFrameTimeMs(), 0.0f);
}
