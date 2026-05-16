// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cpu_profiler.cpp
/// @brief Unit tests for the CpuProfiler.
#include "profiler/cpu_profiler.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace Vestige;

// =============================================================================
// CpuProfilerTest — Frame lifecycle and scope recording
// =============================================================================

TEST(CpuProfilerTest, LastFrameIsEmptyBeforeFirstEndFrame)
{
    CpuProfiler profiler;
    EXPECT_TRUE(profiler.getLastFrame().empty());
    EXPECT_FLOAT_EQ(profiler.getTotalCpuTimeMs(), 0.0f);
}

TEST(CpuProfilerTest, BeginEndFrameRecordsPositiveTime)
{
    CpuProfiler profiler;
    profiler.beginFrame();

    // Burn a small amount of time so the duration is measurably > 0.
    // 5 ms keeps us comfortably above the steady_clock resolution floor
    // (1 ms is within POSIX minimum-sleep-resolution territory under
    // ASAN/LSAN and can return early enough that the profiler reads
    // sub-millisecond values).
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    profiler.endFrame();

    EXPECT_GT(profiler.getTotalCpuTimeMs(), 0.5f);
}

TEST(CpuProfilerTest, PushPopScopeRecordsOneEntry)
{
    CpuProfiler profiler;
    profiler.beginFrame();
    profiler.pushScope("TestScope");
    profiler.popScope();
    profiler.endFrame();

    const auto& frame = profiler.getLastFrame();
    ASSERT_EQ(frame.size(), 1u);
    EXPECT_STREQ(frame[0].name, "TestScope");
    EXPECT_GE(frame[0].endMs, frame[0].startMs);
    EXPECT_EQ(frame[0].parentIndex, -1);
    EXPECT_EQ(frame[0].depth, 0);
}

TEST(CpuProfilerTest, NestedScopesRecordParentAndDepth)
{
    CpuProfiler profiler;
    profiler.beginFrame();

    profiler.pushScope("Outer");
    profiler.pushScope("Inner");
    profiler.popScope();   // Inner
    profiler.popScope();   // Outer

    profiler.endFrame();

    const auto& frame = profiler.getLastFrame();
    ASSERT_EQ(frame.size(), 2u);

    // Outer scope
    EXPECT_STREQ(frame[0].name, "Outer");
    EXPECT_EQ(frame[0].parentIndex, -1);
    EXPECT_EQ(frame[0].depth, 0);

    // Inner scope
    EXPECT_STREQ(frame[1].name, "Inner");
    EXPECT_EQ(frame[1].parentIndex, 0);
    EXPECT_EQ(frame[1].depth, 1);

    // Inner should be fully contained within Outer's time span
    EXPECT_GE(frame[1].startMs, frame[0].startMs);
    EXPECT_LE(frame[1].endMs, frame[0].endMs);
}

TEST(CpuProfilerTest, MultipleScopesAtSameDepth)
{
    CpuProfiler profiler;
    profiler.beginFrame();

    profiler.pushScope("A");
    profiler.popScope();

    profiler.pushScope("B");
    profiler.popScope();

    profiler.pushScope("C");
    profiler.popScope();

    profiler.endFrame();

    const auto& frame = profiler.getLastFrame();
    ASSERT_EQ(frame.size(), 3u);

    EXPECT_STREQ(frame[0].name, "A");
    EXPECT_STREQ(frame[1].name, "B");
    EXPECT_STREQ(frame[2].name, "C");

    for (size_t i = 0; i < 3; i++)
    {
        EXPECT_EQ(frame[i].parentIndex, -1);
        EXPECT_EQ(frame[i].depth, 0);
    }
}

TEST(CpuProfilerTest, BeginFrameClearsPreviousScopes)
{
    CpuProfiler profiler;

    // First frame with some scopes
    profiler.beginFrame();
    profiler.pushScope("OldScope");
    profiler.popScope();
    profiler.endFrame();

    ASSERT_EQ(profiler.getLastFrame().size(), 1u);

    // Second frame with no scopes — should snapshot as empty
    profiler.beginFrame();
    profiler.endFrame();

    EXPECT_TRUE(profiler.getLastFrame().empty());
}

TEST(CpuProfilerTest, ScopeTimingsAreRelativeToFrameStart)
{
    CpuProfiler profiler;
    profiler.beginFrame();

    // The very first scope's start time should be very close to zero
    // (i.e., relative to beginFrame, not epoch).
    profiler.pushScope("First");
    profiler.popScope();
    profiler.endFrame();

    const auto& frame = profiler.getLastFrame();
    ASSERT_EQ(frame.size(), 1u);

    // Start time should be very small (< 100ms even on a slow system)
    EXPECT_LT(frame[0].startMs, 100.0f);
}

TEST(CpuProfilerTest, PopScopeOnEmptyStackDoesNotCrash)
{
    CpuProfiler profiler;
    profiler.beginFrame();

    // Pop without any push — should be a no-op, not a crash
    EXPECT_NO_THROW(profiler.popScope());

    profiler.endFrame();
    EXPECT_TRUE(profiler.getLastFrame().empty());
}

TEST(CpuProfilerTest, DeeplyNestedScopes)
{
    CpuProfiler profiler;
    profiler.beginFrame();

    constexpr int DEPTH = 5;
    for (int i = 0; i < DEPTH; i++)
    {
        profiler.pushScope("Level");
    }
    for (int i = 0; i < DEPTH; i++)
    {
        profiler.popScope();
    }

    profiler.endFrame();

    const auto& frame = profiler.getLastFrame();
    ASSERT_EQ(frame.size(), static_cast<size_t>(DEPTH));

    for (int i = 0; i < DEPTH; i++)
    {
        EXPECT_EQ(frame[static_cast<size_t>(i)].depth, i);

        if (i == 0)
        {
            EXPECT_EQ(frame[static_cast<size_t>(i)].parentIndex, -1);
        }
        else
        {
            EXPECT_EQ(frame[static_cast<size_t>(i)].parentIndex, i - 1);
        }
    }
}
