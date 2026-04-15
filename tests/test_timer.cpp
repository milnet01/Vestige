// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_timer.cpp
/// @brief Unit tests for the Timer class.
/// @details Timer uses `std::chrono::steady_clock` internally, so these
///          tests are pure unit tests — no GLFW / windowing / display
///          dependency. Adding a monotonic-sleep test to verify that
///          elapsed time actually advances in wall time.
#include "core/timer.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

TEST(TimerTest, InitialState)
{
    Vestige::Timer timer;
    // Before first update, delta time should be 0
    EXPECT_FLOAT_EQ(timer.getDeltaTime(), 0.0f);
    EXPECT_EQ(timer.getFps(), 0);
}

TEST(TimerTest, UpdateReturnsDeltaTime)
{
    Vestige::Timer timer;
    float dt = timer.update();
    // Delta time should be non-negative
    EXPECT_GE(dt, 0.0f);
    // And should match getDeltaTime()
    EXPECT_FLOAT_EQ(dt, timer.getDeltaTime());
}

TEST(TimerTest, DeltaTimeIsClamped)
{
    Vestige::Timer timer;
    float dt = timer.update();
    // Delta time should never exceed the clamp value of 0.25 seconds
    EXPECT_LE(dt, 0.25f);
}

TEST(TimerTest, ElapsedTimeIncreases)
{
    Vestige::Timer timer;
    double time1 = timer.getElapsedTime();
    timer.update();
    double time2 = timer.getElapsedTime();
    EXPECT_GE(time2, time1);
}

TEST(TimerTest, ElapsedTimeAdvancesWithWallClock)
{
    Vestige::Timer timer;
    double before = timer.getElapsedTime();
    // Sleep for a measurable-but-short interval. 10 ms is large enough
    // that steady_clock resolution on every supported platform will
    // register the advance, small enough that the test stays fast.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    double after = timer.getElapsedTime();
    EXPECT_GT(after - before, 0.005);  // at least 5 ms elapsed
}

TEST(TimerTest, FrameRateCapRoundTrip)
{
    Vestige::Timer timer;
    EXPECT_EQ(timer.getFrameRateCap(), 0);  // uncapped by default

    timer.setFrameRateCap(60);
    EXPECT_EQ(timer.getFrameRateCap(), 60);

    timer.setFrameRateCap(0);
    EXPECT_EQ(timer.getFrameRateCap(), 0);
}
