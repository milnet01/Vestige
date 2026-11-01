// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_timer.cpp
/// @brief GpuTimer behaviour that needs a real GL context (skips without one).

#include "gl_test_fixture.h"
#include "profiler/gpu_timer.h"

#include <glad/gl.h>
#include <gtest/gtest.h>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

/// Runs one frame with the given number of flat passes.
void runFrame(GpuTimer& timer, int passes)
{
    timer.beginFrame();
    for (int i = 0; i < passes; ++i)
    {
        timer.beginPass("Pass");
        timer.endPass();
    }
    timer.markFrameEnd();
    timer.endFrame();
}

class GpuTimerTest : public GLTestFixture {};

}  // namespace

// 3D_E-0788: once warm, the timer reads a sample each frame and says so.
TEST_F(GpuTimerTest, WarmTimerReportsFreshResults)
{
    GpuTimer timer;
    timer.init();
    for (int i = 0; i < GpuTimer::BUFFER_COUNT + 1; ++i)
    {
        runFrame(timer, 2);
        glFinish();  // test only: make sure the queries are done
    }
    EXPECT_TRUE(timer.hasResults());
    EXPECT_TRUE(timer.hasFreshResults());
    EXPECT_EQ(timer.getResults().size(), 2u);
}

// Before warm-up there is no sample to call fresh.
TEST_F(GpuTimerTest, ColdTimerHasNoFreshResults)
{
    GpuTimer timer;
    timer.init();
    runFrame(timer, 1);
    EXPECT_FALSE(timer.hasFreshResults());
}

// 3D_E-0790: a pass begun inside another is refused loudly, and the outer
// pass still reports as one pass.
TEST_F(GpuTimerTest, NestedPassIsCountedAndOuterPassSurvives)
{
    GpuTimer timer;
    timer.init();
    for (int i = 0; i < GpuTimer::BUFFER_COUNT + 1; ++i)
    {
        timer.beginFrame();
        timer.beginPass("Outer");
        timer.beginPass("Inner");
        timer.endPass();
        timer.endPass();
        timer.markFrameEnd();
        timer.endFrame();
        glFinish();
    }
    EXPECT_EQ(timer.getNestingViolations(), GpuTimer::BUFFER_COUNT + 1);
    ASSERT_EQ(timer.getResults().size(), 1u);
    EXPECT_EQ(timer.getResults()[0].name, "Outer");
}

// 3D_E-0790: the 17th pass in a frame is counted, not lost silently, and the
// first 16 still report.
TEST_F(GpuTimerTest, PassesBeyondTheLimitAreCounted)
{
    GpuTimer timer;
    timer.init();
    for (int i = 0; i < GpuTimer::BUFFER_COUNT + 1; ++i)
    {
        runFrame(timer, 17);
        glFinish();
    }
    EXPECT_EQ(timer.getDroppedPasses(), GpuTimer::BUFFER_COUNT + 1);
    EXPECT_EQ(timer.getResults().size(), 16u);
}

// Flat, in-limit use stays clean.
TEST_F(GpuTimerTest, FlatPassesRaiseNoViolations)
{
    GpuTimer timer;
    timer.init();
    for (int i = 0; i < GpuTimer::BUFFER_COUNT + 1; ++i)
    {
        runFrame(timer, 16);
    }
    EXPECT_EQ(timer.getNestingViolations(), 0);
    EXPECT_EQ(timer.getDroppedPasses(), 0);
}

// 3D_E-0789: the remainder is the frame span minus the passes, never negative.
TEST(GpuTimerRemainder, SpanMinusPasses)
{
    EXPECT_DOUBLE_EQ(GpuTimer::unattributedMs(10.0, 7.5), 2.5);
}

TEST(GpuTimerRemainder, NeverNegative)
{
    EXPECT_DOUBLE_EQ(GpuTimer::unattributedMs(5.0, 5.2), 0.0);
}

TEST_F(GpuTimerTest, UnattributedIsZeroBeforeAnySpan)
{
    GpuTimer timer;
    timer.init();
    EXPECT_DOUBLE_EQ(timer.getUnattributedMs(), 0.0);
}
