// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ambient_system.cpp
/// @brief 3D_E-S0016 — AmbientSystem's pure helpers: the bed planner
///        (INV-3), the clock (INV-4) and the one-shot scheduler (INV-5),
///        driven frame by frame without an audio device.
#include "systems/ambient_system.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

using namespace Vestige;

namespace
{

constexpr std::uint32_t kZone = 5;
constexpr float kFrame = 1.0f / 60.0f;

/// Stand-in for the AL source pool: a playing source carries the ticket of
/// the playback holding it.
struct FakePool
{
    std::unordered_map<unsigned int, std::uint64_t> playing;
    std::uint64_t nextTicket = 1;

    std::uint64_t grant(unsigned int source)
    {
        playing[source] = nextTicket;
        return nextTicket++;
    }

    AmbientBedOwnsFn owns() const
    {
        return [this](const AmbientBed& bed)
        {
            auto it = playing.find(bed.source);
            return bed.ticket != 0 && it != playing.end() && it->second == bed.ticket;
        };
    }
};

using Beds = std::unordered_map<std::uint32_t, AmbientBed>;

std::vector<AmbientBedAction> frame(Beds& beds, const FakePool& pool, float weight,
                                    const std::string& clip = "wind.ogg",
                                    float dt = kFrame, bool device = true)
{
    return planAmbientBeds(beds, {{kZone, clip, weight}}, pool.owns(), dt, device);
}

/// Applies a plan's Starts the way AmbientSystem does, granting `source`.
void startAll(Beds& beds, FakePool& pool, const std::vector<AmbientBedAction>& actions,
              unsigned int source)
{
    for (const AmbientBedAction& a : actions)
    {
        if (a.kind == AmbientBedAction::Kind::Start)
        {
            const std::uint64_t ticket = source != 0 ? pool.grant(source) : 0;
            recordAmbientBedStart(beds[a.entityId], source, ticket);
        }
    }
}

bool touches(const std::vector<AmbientBedAction>& actions, unsigned int source)
{
    for (const AmbientBedAction& a : actions)
    {
        if (a.kind != AmbientBedAction::Kind::Start && a.source == source)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

// ----- INV-3 — beds ----------------------------------------------------

TEST(AmbientBedPlanner, AudibleZoneStartsThenFollowsItsWeight)
{
    Beds beds;
    FakePool pool;

    auto actions = frame(beds, pool, 0.5f);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].kind, AmbientBedAction::Kind::Start);
    EXPECT_EQ(actions[0].clip, "wind.ogg");
    EXPECT_FLOAT_EQ(actions[0].volume, 0.5f);
    startAll(beds, pool, actions, 100);

    actions = frame(beds, pool, 0.25f);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].kind, AmbientBedAction::Kind::SetVolume);
    EXPECT_EQ(actions[0].source, 100u);
    EXPECT_FLOAT_EQ(actions[0].volume, 0.25f);
}

TEST(AmbientBedPlanner, SilentZoneStopsItsBed)
{
    Beds beds;
    FakePool pool;
    startAll(beds, pool, frame(beds, pool, 1.0f), 100);

    const auto actions = frame(beds, pool, 0.0f);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].kind, AmbientBedAction::Kind::Stop);
    EXPECT_EQ(actions[0].source, 100u);
    EXPECT_EQ(beds[kZone].source, 0u) << "a silent zone still holds a source";
}

TEST(AmbientBedPlanner, RemovedZoneStopsAndIsForgotten)
{
    Beds beds;
    FakePool pool;
    startAll(beds, pool, frame(beds, pool, 1.0f), 100);

    const auto actions = planAmbientBeds(beds, {}, pool.owns(), kFrame, true);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].kind, AmbientBedAction::Kind::Stop);
    EXPECT_TRUE(beds.empty());
}

TEST(AmbientBedPlanner, NeverTouchesASourceTakenByAnotherSound)
{
    Beds beds;
    FakePool pool;
    startAll(beds, pool, frame(beds, pool, 1.0f), 100);
    pool.grant(100);  // evicted: source 100 now plays another sound

    auto actions = frame(beds, pool, 0.0f);
    EXPECT_FALSE(touches(actions, 100)) << "stopped the sound that now holds 100";

    startAll(beds, pool, frame(beds, pool, 1.0f), 100);
    pool.grant(100);
    actions = frame(beds, pool, 1.0f);
    EXPECT_FALSE(touches(actions, 100)) << "steered the sound that now holds 100";
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].kind, AmbientBedAction::Kind::Start)
        << "a bed lost to eviction restarts at once";
}

TEST(AmbientBedPlanner, ClipChangeStopsAndRestartsInOneFrame)
{
    Beds beds;
    FakePool pool;
    startAll(beds, pool, frame(beds, pool, 1.0f, "wind.ogg"), 100);

    const auto actions = frame(beds, pool, 1.0f, "rain.ogg");
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].kind, AmbientBedAction::Kind::Stop);
    EXPECT_EQ(actions[1].kind, AmbientBedAction::Kind::Start);
    EXPECT_EQ(actions[1].clip, "rain.ogg");
}

TEST(AmbientBedPlanner, EmptyClipOrNoDeviceStartsNothing)
{
    Beds beds;
    FakePool pool;
    EXPECT_TRUE(frame(beds, pool, 1.0f, "").empty());
    EXPECT_TRUE(frame(beds, pool, 1.0f, "wind.ogg", kFrame, false).empty());
}

TEST(AmbientBedPlanner, FailedStartsBackOff)
{
    Beds beds;
    FakePool pool;

    // Failed starts at t = 0, 1, 3, 7: waits of 1, 2 and 4 seconds.
    const float expectedWaits[] = {1.0f, 2.0f, 4.0f};
    startAll(beds, pool, frame(beds, pool, 1.0f), 0);
    for (float wait : expectedWaits)
    {
        EXPECT_TRUE(frame(beds, pool, 1.0f, "wind.ogg", wait - 0.01f).empty())
            << "retried before the " << wait << " s back-off ended";
        const auto actions = frame(beds, pool, 1.0f, "wind.ogg", 0.02f);
        ASSERT_EQ(actions.size(), 1u);
        EXPECT_EQ(actions[0].kind, AmbientBedAction::Kind::Start);
        startAll(beds, pool, actions, 0);
    }
    EXPECT_FLOAT_EQ(ambientRetryDelay(7), 60.0f);
    EXPECT_FLOAT_EQ(ambientRetryDelay(1000), 60.0f);

    // A silent frame resets the count: the next failure waits 1 s again.
    EXPECT_TRUE(frame(beds, pool, 0.0f).empty());
    startAll(beds, pool, frame(beds, pool, 1.0f), 0);
    EXPECT_FLOAT_EQ(beds[kZone].retryIn, 1.0f);
}

// ----- INV-4 — clock ---------------------------------------------------

TEST(AmbientClock, AdvancesAndWraps)
{
    EXPECT_NEAR(advanceAmbientHour(23.5f, 60.0f, 1.0f), 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(advanceAmbientHour(9.0f, 60.0f, 0.0f), 9.0f);
    EXPECT_NEAR(advanceAmbientHour(0.5f, 60.0f, -1.0f), 23.5f, 1e-4f);
}

// ----- INV-5 — one-shots -----------------------------------------------

TEST(AmbientOneShot, ArmedWhenSeenAndSilentTimeDoesNotCount)
{
    const UniformSampleFn zero = [] { return 0.0f; };
    RandomOneShotScheduler s;  // 15–45 s; sample 0 draws 15 s
    armAmbientOneShot(s, zero);

    for (int i = 0; i < 100; ++i)
    {
        EXPECT_FALSE(tickAmbientOneShot(s, 0.0f, 1.0f, zero));
    }
    for (int audible = 1; audible <= 15; ++audible)
    {
        const bool fired = tickAmbientOneShot(s, 0.5f, 1.0f, zero);
        EXPECT_EQ(fired, audible == 15) << "audible frame " << audible;
    }
}

TEST(AmbientOneShot, PositionStaysWithinTheCoreOnTheZonePlane)
{
    const glm::vec3 center(1.0f, 2.0f, 3.0f);
    const float samples[][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.25f}};
    for (const auto& u : samples)
    {
        const glm::vec3 p = ambientOneShotPosition(center, 4.0f, u[0], u[1]);
        EXPECT_FLOAT_EQ(p.y, 2.0f);
        EXPECT_LE(glm::length(p - center), 4.0f + 1e-4f);
    }
}

TEST(AmbientOneShot, ClipIndexCoversTheList)
{
    EXPECT_EQ(ambientOneShotClipIndex(3, 0.0f), 0u);
    EXPECT_EQ(ambientOneShotClipIndex(3, 0.5f), 1u);
    EXPECT_EQ(ambientOneShotClipIndex(3, 1.0f), 2u);
    EXPECT_EQ(ambientOneShotClipIndex(0, 0.5f), 0u);
}
