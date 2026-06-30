// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_footstep_system.cpp
/// @brief AX4 S6 — footstep cadence + landing logic. Exercises the pure,
///        engine-free pieces (distance-based stride accumulator, stride-length
///        curve, landing edge) and the system's no-controller safety guard.
#include "systems/footstep_system.h"

#include <gtest/gtest.h>

namespace
{

/// Run the accumulator at a constant speed for `seconds` and count the steps.
int countSteps(float speedMps, float seconds, float dt)
{
    Vestige::StrideAccumulator acc;
    int steps = 0;
    for (float t = 0.0f; t < seconds; t += dt)
    {
        if (acc.tick(speedMps, dt))
        {
            ++steps;
        }
    }
    return steps;
}

}  // namespace

using namespace Vestige;

// --- Stride-length curve ---------------------------------------------------

TEST(StrideLength, LengthensWithSpeed)
{
    EXPECT_LT(strideLengthMeters(0.0f), strideLengthMeters(2.0f));
    EXPECT_LT(strideLengthMeters(2.0f), strideLengthMeters(6.0f));
    EXPECT_GT(strideLengthMeters(0.0f), 0.0f);
}

// --- Distance-based cadence ------------------------------------------------

TEST(StrideAccumulator, FiresAfterOneStrideOfDistance)
{
    constexpr float speed = 1.4f;   // a brisk walk
    constexpr float dt = 1.0f / 120.0f;
    const float stride = strideLengthMeters(speed);

    StrideAccumulator acc;
    int ticks = 0;
    while (!acc.tick(speed, dt))
    {
        ++ticks;
        ASSERT_LT(ticks, 100000) << "accumulator never fired";
    }
    ++ticks;  // the firing tick
    const float distanceAtFire = static_cast<float>(ticks) * speed * dt;
    // The step fires the first frame cumulative distance crosses one stride,
    // so it lands in [stride, stride + one-frame-of-travel).
    EXPECT_GE(distanceAtFire, stride);
    EXPECT_LT(distanceAtFire, stride + speed * dt);
}

TEST(StrideAccumulator, StandingStillNeverFires)
{
    StrideAccumulator acc;
    for (int i = 0; i < 1000; ++i)
    {
        EXPECT_FALSE(acc.tick(0.1f, 1.0f / 120.0f));  // below the walk-speed floor
    }
    EXPECT_FLOAT_EQ(acc.m_distance, 0.0f) << "no distance is banked while standing";
}

TEST(StrideAccumulator, RunningHasHigherCadenceThanWalking)
{
    constexpr float dt = 1.0f / 120.0f;
    const int walkSteps = countSteps(1.4f, 4.0f, dt);
    const int runSteps  = countSteps(5.0f, 4.0f, dt);
    EXPECT_GT(runSteps, walkSteps);
    EXPECT_GT(walkSteps, 0);
}

TEST(StrideAccumulator, CadenceTracksDistanceWithoutDrift)
{
    // At constant speed, step count must match total distance / stride to
    // within one step — the carried remainder keeps cadence from drifting as
    // the per-frame overshoot would otherwise accumulate or be discarded.
    constexpr float speed = 3.0f;
    constexpr float seconds = 10.0f;
    constexpr float dt = 1.0f / 120.0f;
    const int steps = countSteps(speed, seconds, dt);
    const float ideal = (speed * seconds) / strideLengthMeters(speed);
    EXPECT_NEAR(static_cast<float>(steps), ideal, 1.0f);
}

// --- Landing edge ----------------------------------------------------------

TEST(Landing, FiresOnHardDescentEdge)
{
    // airborne (false) -> grounded (true) with a fast descent = a landing.
    EXPECT_TRUE(landingTriggered(/*wasOnGround*/ false, /*onGround*/ true, /*descent*/ 6.0f));
}

TEST(Landing, IgnoresSoftStepDown)
{
    // Same edge, but a gentle descent stays a normal step, not a thud.
    EXPECT_FALSE(landingTriggered(false, true, 0.4f));
}

TEST(Landing, IgnoresContinuousGroundContact)
{
    // Walking along the ground (no airborne→ground edge) is never a landing,
    // however the descent figure reads.
    EXPECT_FALSE(landingTriggered(/*wasOnGround*/ true, /*onGround*/ true, 6.0f));
}

TEST(Landing, IgnoresStillAirborne)
{
    EXPECT_FALSE(landingTriggered(/*wasOnGround*/ false, /*onGround*/ false, 6.0f));
}

// --- System safety ---------------------------------------------------------

TEST(FootstepSystem, UpdateBeforeInitializeIsANoOp)
{
    // No controller / no audio resolved yet — update must early-out, not
    // dereference its null dependencies.
    FootstepSystem system;
    EXPECT_NO_FATAL_FAILURE(system.update(1.0f / 60.0f));
}
