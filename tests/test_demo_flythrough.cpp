// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_demo_flythrough.cpp
/// @brief Unit tests for DemoFlythrough (--demo-flythrough camera path).
#include <gtest/gtest.h>

#include <cmath>

#include "testing/demo_flythrough.h"

using namespace Vestige;

namespace
{
/// A straight 60 m path along +X at 6 m/s: 10 s of travel.
DemoFlythrough straightPath()
{
    DemoFlythrough f;
    f.addKey({0.0f, 2.0f, 0.0f}, {10.0f, 2.0f, 0.0f});
    f.addKey({30.0f, 2.0f, 0.0f}, {40.0f, 2.0f, 0.0f});
    f.addKey({60.0f, 2.0f, 0.0f}, {70.0f, 2.0f, 0.0f});
    f.setSpeed(6.0f);
    f.setStartHold(2.0f);
    f.setEndHold(1.0f);
    return f;
}
} // namespace

TEST(DemoFlythroughTest, EmptyPathIsDoneImmediately)
{
    DemoFlythrough f;
    EXPECT_TRUE(f.poseAt(0.0f).done);
}

TEST(DemoFlythroughTest, TotalTimeIsHoldsPlusLengthOverSpeed)
{
    const DemoFlythrough f = straightPath();
    EXPECT_NEAR(f.travelSeconds(), 10.0f, 0.05f);
    EXPECT_NEAR(f.totalSeconds(), 13.0f, 0.05f);
}

TEST(DemoFlythroughTest, HoldsStillAtStartAndEnd)
{
    const DemoFlythrough f = straightPath();
    EXPECT_NEAR(f.poseAt(0.0f).eye.x, 0.0f, 1e-3f);
    EXPECT_NEAR(f.poseAt(1.9f).eye.x, 0.0f, 1e-3f);
    EXPECT_NEAR(f.poseAt(12.1f).eye.x, 60.0f, 1e-2f);
    EXPECT_FALSE(f.poseAt(12.9f).done);
    EXPECT_TRUE(f.poseAt(13.1f).done);
}

TEST(DemoFlythroughTest, EasedMotionIsMonotoneAndHalfwayAtMidpoint)
{
    const DemoFlythrough f = straightPath();
    // Smootherstep is symmetric, so half the travel time is half the path.
    EXPECT_NEAR(f.poseAt(7.0f).eye.x, 30.0f, 0.5f);
    float prev = -1.0f;
    for (float t = 2.0f; t <= 12.0f; t += 0.1f)
    {
        const float x = f.poseAt(t).eye.x;
        EXPECT_GE(x, prev - 1e-3f) << "moved backwards at t=" << t;
        prev = x;
    }
}

TEST(DemoFlythroughTest, FacesTheLookAtPoint)
{
    const DemoFlythrough f = straightPath();
    // Looking along +X is yaw 0 in Camera's convention; level is pitch 0.
    const FlythroughPose p = f.poseAt(5.0f);
    EXPECT_NEAR(p.yaw, 0.0f, 0.5f);
    EXPECT_NEAR(p.pitch, 0.0f, 0.5f);
}

TEST(DemoFlythroughTest, GroundClearanceLiftsTheEye)
{
    DemoFlythrough f = straightPath();
    // A 10 m hill everywhere: the 2 m path would be buried without the clamp.
    f.setGroundClearance([](float, float) { return 10.0f; }, 1.5f);
    EXPECT_NEAR(f.poseAt(5.0f).eye.y, 11.5f, 1e-3f);
}
