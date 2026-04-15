// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_timer.cpp
/// @brief Unit tests for the Timer class.
/// @details Timer depends on GLFW for time, so these tests are limited
///          to verifying the interface and basic behavior.
#include "core/timer.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

class TimerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Timer uses glfwGetTime(), so GLFW must be initialized
        glfwInit();
    }

    void TearDown() override
    {
        glfwTerminate();
    }
};

TEST_F(TimerTest, InitialState)
{
    Vestige::Timer timer;
    // Before first update, delta time should be 0
    EXPECT_FLOAT_EQ(timer.getDeltaTime(), 0.0f);
    EXPECT_EQ(timer.getFps(), 0);
}

TEST_F(TimerTest, UpdateReturnsDeltaTime)
{
    Vestige::Timer timer;
    float dt = timer.update();
    // Delta time should be non-negative
    EXPECT_GE(dt, 0.0f);
    // And should match getDeltaTime()
    EXPECT_FLOAT_EQ(dt, timer.getDeltaTime());
}

TEST_F(TimerTest, DeltaTimeIsClamped)
{
    Vestige::Timer timer;
    float dt = timer.update();
    // Delta time should never exceed the clamp value of 0.25 seconds
    EXPECT_LE(dt, 0.25f);
}

TEST_F(TimerTest, ElapsedTimeIncreases)
{
    Vestige::Timer timer;
    double time1 = timer.getElapsedTime();
    timer.update();
    double time2 = timer.getElapsedTime();
    EXPECT_GE(time2, time1);
}
