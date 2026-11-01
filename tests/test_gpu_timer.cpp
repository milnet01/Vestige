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
