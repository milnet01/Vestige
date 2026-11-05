// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_timer.cpp
/// @brief GpuTimer behaviour that needs a real GL context (skips without one).

#include "gl_test_fixture.h"
#include "profiler/gpu_timer.h"
#include "profiler/performance_profiler.h"
#include "profiler/profile_log.h"
#include "test_helpers.h"

#include <glad/gl.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

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

// 3D_E-0792 (CODING_STANDARDS section 29): every pass is a GL debug group, so a
// RenderDoc or apitrace capture reads as named passes. A refused nested pass
// still pushes and pops, so the debug-group stack stays balanced.
TEST_F(GpuTimerTest, EachPassIsADebugGroupAndTheStackBalances)
{
    GpuTimer timer;
    timer.init();
    GLint base = 0;
    glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &base);

    timer.beginFrame();
    timer.beginPass("Outer");
    GLint inPass = 0;
    glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &inPass);
    timer.beginPass("Nested");
    GLint nested = 0;
    glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &nested);
    timer.endPass();
    timer.endPass();
    timer.endPass();  // unmatched: refused, must not pop the caller's group
    GLint after = 0;
    glGetIntegerv(GL_DEBUG_GROUP_STACK_DEPTH, &after);
    timer.markFrameEnd();
    timer.endFrame();

    EXPECT_EQ(inPass, base + 1);
    EXPECT_EQ(nested, base + 2);
    EXPECT_EQ(after, base);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
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

// 3D_E-0789: the profile log's gpu,total is the whole frame, as the
// performance panel shows it, and the GPU work outside every pass gets its own
// gpu,unattributed row. It used to write the sum of the passes as the total, so
// untimed work vanished from the log.
TEST_F(GpuTimerTest, ProfileLogTotalIsTheFrameAndNamesTheRemainder)
{
    const std::string path = (std::filesystem::temp_directory_path()
        / ("vestige_gpu_total_" + Testing::vestigeTestStamp() + ".csv")).string();
    PerformanceProfiler profiler;
    profiler.init();
    GpuTimer& timer = profiler.getGpuTimer();
    ProfileLog log;
    ASSERT_TRUE(log.open(path));

    for (int i = 0; i < 4 * GpuTimer::BUFFER_COUNT; ++i)
    {
        timer.beginFrame();
        glClear(GL_COLOR_BUFFER_BIT);  // GPU work outside every pass
        glFinish();
        timer.beginPass("Pass");
        timer.endPass();
        timer.markFrameEnd();
        glFinish();  // test only: make sure the queries are done
        profiler.endFrame(0.016f);
        log.sample(profiler, i * 0.25);
    }
    log.close();

    double total = -1.0;
    double pass = -1.0;
    double unattributed = -1.0;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
    {
        auto valueOf = [&line](const std::string& key, double& out)
        {
            const auto at = line.find(key);
            if (at != std::string::npos && out < 0.0)
            {
                const auto start = at + key.size();
                out = std::stod(line.substr(start, line.rfind(',') - start));
            }
        };
        valueOf(",gpu,total,0,", total);
        valueOf(",gpu,Pass,0,", pass);
        valueOf(",gpu,unattributed,0,", unattributed);
    }
    in.close();
    std::filesystem::remove(path);

    if (!timer.hasFrameSpan()) GTEST_SKIP() << "no GPU timestamp queries in this context";
    ASSERT_GE(unattributed, 0.0) << "the log has no gpu,unattributed row";
    ASSERT_GE(pass, 0.0);
    EXPECT_NEAR(total, pass + unattributed, 0.002)
        << "gpu,total must be the whole frame: its passes plus the remainder";
}
