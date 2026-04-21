// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_ambient.cpp
/// @brief Phase 10 coverage for ambient zones + time-of-day
///        weighting + random one-shot scheduler.

#include <gtest/gtest.h>

#include "audio/audio_ambient.h"

#include <queue>

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-3f;  // Wider epsilon for triangle-based weights.
}

// -- Ambient zone volume ------------------------------------------

TEST(AmbientZone, InsideCoreReturnsMaxVolume)
{
    AmbientZone z;
    z.coreRadius  = 10.0f;
    z.falloffBand =  5.0f;
    z.maxVolume   =  0.8f;

    EXPECT_NEAR(computeAmbientZoneVolume(z, 0.0f),  0.8f, kEps);
    EXPECT_NEAR(computeAmbientZoneVolume(z, 10.0f), 0.8f, kEps);
}

TEST(AmbientZone, OutsideFalloffReturnsZero)
{
    AmbientZone z;
    z.coreRadius  = 10.0f;
    z.falloffBand =  5.0f;
    z.maxVolume   =  0.8f;

    EXPECT_NEAR(computeAmbientZoneVolume(z, 15.0f),  0.0f, kEps);
    EXPECT_NEAR(computeAmbientZoneVolume(z, 100.0f), 0.0f, kEps);
}

TEST(AmbientZone, MidFalloffIsLinear)
{
    AmbientZone z;
    z.coreRadius  = 10.0f;
    z.falloffBand =  4.0f;
    z.maxVolume   =  1.0f;

    EXPECT_NEAR(computeAmbientZoneVolume(z, 12.0f), 0.5f, kEps);
}

TEST(AmbientZone, VolumeClampsAboveOne)
{
    AmbientZone z;
    z.coreRadius  = 10.0f;
    z.falloffBand =  0.0f;
    z.maxVolume   =  2.0f;  // unreasonable — clamped to 1.0

    EXPECT_NEAR(computeAmbientZoneVolume(z, 0.0f), 1.0f, kEps);
}

// -- Time-of-day window labels ------------------------------------

TEST(TimeOfDay, WindowLabelsAreStable)
{
    EXPECT_STREQ(timeOfDayWindowLabel(TimeOfDayWindow::Dawn),  "Dawn");
    EXPECT_STREQ(timeOfDayWindowLabel(TimeOfDayWindow::Day),   "Day");
    EXPECT_STREQ(timeOfDayWindowLabel(TimeOfDayWindow::Dusk),  "Dusk");
    EXPECT_STREQ(timeOfDayWindowLabel(TimeOfDayWindow::Night), "Night");
}

// -- Time-of-day weighting ----------------------------------------

TEST(TimeOfDay, WeightsAlwaysSumToOne)
{
    // Sample the clock densely — normalisation inside
    // computeTimeOfDayWeights should guarantee sum = 1.
    for (float h = 0.0f; h < 24.0f; h += 0.25f)
    {
        auto w = computeTimeOfDayWeights(h);
        const float sum = w.dawn + w.day + w.dusk + w.night;
        EXPECT_NEAR(sum, 1.0f, kEps) << "hour=" << h;
    }
}

TEST(TimeOfDay, AtEachPeakTheMatchingWindowDominates)
{
    // The normalisation plus non-trivial overlap means no window hits
    // a perfect 1.0, but at its peak it must exceed all the others.
    {
        auto w = computeTimeOfDayWeights(6.0f);
        EXPECT_GT(w.dawn, w.day);
        EXPECT_GT(w.dawn, w.dusk);
        EXPECT_GT(w.dawn, w.night);
    }
    {
        auto w = computeTimeOfDayWeights(13.0f);
        EXPECT_GT(w.day, w.dawn);
        EXPECT_GT(w.day, w.dusk);
        EXPECT_GT(w.day, w.night);
    }
    {
        auto w = computeTimeOfDayWeights(20.0f);
        EXPECT_GT(w.dusk, w.dawn);
        EXPECT_GT(w.dusk, w.day);
        EXPECT_GT(w.dusk, w.night);
    }
    {
        auto w = computeTimeOfDayWeights(1.0f);
        EXPECT_GT(w.night, w.dawn);
        EXPECT_GT(w.night, w.day);
        EXPECT_GT(w.night, w.dusk);
    }
}

TEST(TimeOfDay, WrapsAroundModuloTwentyFour)
{
    // Same result at the equivalent hours on either side of wrap.
    auto wA = computeTimeOfDayWeights(25.0f);
    auto wB = computeTimeOfDayWeights(1.0f);
    EXPECT_NEAR(wA.dawn,  wB.dawn,  kEps);
    EXPECT_NEAR(wA.day,   wB.day,   kEps);
    EXPECT_NEAR(wA.dusk,  wB.dusk,  kEps);
    EXPECT_NEAR(wA.night, wB.night, kEps);

    auto wC = computeTimeOfDayWeights(-2.0f);  // equivalent to hour 22
    auto wD = computeTimeOfDayWeights(22.0f);
    EXPECT_NEAR(wC.dawn,  wD.dawn,  kEps);
    EXPECT_NEAR(wC.day,   wD.day,   kEps);
    EXPECT_NEAR(wC.dusk,  wD.dusk,  kEps);
    EXPECT_NEAR(wC.night, wD.night, kEps);
}

TEST(TimeOfDay, MidnightNightDominates)
{
    // 00:00 — night peak sits at 1.0, so night should dominate even
    // across the wrap boundary.
    auto w = computeTimeOfDayWeights(0.0f);
    EXPECT_GT(w.night, w.dawn);
    EXPECT_GT(w.night, w.day);
    EXPECT_GT(w.night, w.dusk);
}

// -- Random one-shot scheduler -----------------------------------

TEST(RandomOneShot, FiresWhenCooldownExpires)
{
    RandomOneShotScheduler s;
    s.minIntervalSeconds = 2.0f;
    s.maxIntervalSeconds = 4.0f;
    s.timeUntilNextFire  = 0.0f;   // armed immediately

    auto sampler = []() { return 0.5f; };  // midpoint → 3 s next interval

    EXPECT_TRUE(tickRandomOneShot(s, 0.016f, sampler));
    EXPECT_NEAR(s.timeUntilNextFire, 3.0f, kEps);
}

TEST(RandomOneShot, DoesNotFireWithCooldownRemaining)
{
    RandomOneShotScheduler s;
    s.timeUntilNextFire = 5.0f;
    auto sampler = []() { return 0.5f; };
    EXPECT_FALSE(tickRandomOneShot(s, 1.0f, sampler));
    EXPECT_NEAR(s.timeUntilNextFire, 4.0f, kEps);
}

TEST(RandomOneShot, OnlyFiresOncePerTick)
{
    // Even with a huge deltaSeconds, a single tick fires at most once.
    RandomOneShotScheduler s;
    s.minIntervalSeconds = 1.0f;
    s.maxIntervalSeconds = 1.0f;
    s.timeUntilNextFire  = 0.0f;

    auto sampler = []() { return 0.0f; };  // always the min interval
    EXPECT_TRUE(tickRandomOneShot(s, 1e6f, sampler));
    // After the fire, we should be re-armed (not negative / not zero).
    EXPECT_GT(s.timeUntilNextFire, 0.0f);
}

TEST(RandomOneShot, UsesSamplerValueForIntervalSelection)
{
    RandomOneShotScheduler s;
    s.minIntervalSeconds = 10.0f;
    s.maxIntervalSeconds = 20.0f;
    s.timeUntilNextFire  = 0.0f;

    auto zero = []() { return 0.0f; };
    EXPECT_TRUE(tickRandomOneShot(s, 0.0f, zero));
    EXPECT_NEAR(s.timeUntilNextFire, 10.0f, kEps);

    s.timeUntilNextFire = 0.0f;
    auto one = []() { return 1.0f; };
    EXPECT_TRUE(tickRandomOneShot(s, 0.0f, one));
    EXPECT_NEAR(s.timeUntilNextFire, 20.0f, kEps);
}

TEST(RandomOneShot, ClampsSamplerOutOfRange)
{
    RandomOneShotScheduler s;
    s.minIntervalSeconds = 10.0f;
    s.maxIntervalSeconds = 20.0f;
    s.timeUntilNextFire  = 0.0f;

    auto over = []() { return 2.0f; };
    EXPECT_TRUE(tickRandomOneShot(s, 0.0f, over));
    EXPECT_NEAR(s.timeUntilNextFire, 20.0f, kEps);

    s.timeUntilNextFire = 0.0f;
    auto under = []() { return -1.0f; };
    EXPECT_TRUE(tickRandomOneShot(s, 0.0f, under));
    EXPECT_NEAR(s.timeUntilNextFire, 10.0f, kEps);
}

TEST(RandomOneShot, NullSamplerFallsBackToMidpoint)
{
    RandomOneShotScheduler s;
    s.minIntervalSeconds = 10.0f;
    s.maxIntervalSeconds = 30.0f;
    s.timeUntilNextFire  = 0.0f;

    EXPECT_TRUE(tickRandomOneShot(s, 0.0f, {}));
    EXPECT_NEAR(s.timeUntilNextFire, 20.0f, kEps);
}

TEST(RandomOneShot, NegativeDeltaIsTreatedAsZero)
{
    RandomOneShotScheduler s;
    s.timeUntilNextFire = 5.0f;
    auto sampler = []() { return 0.5f; };
    EXPECT_FALSE(tickRandomOneShot(s, -10.0f, sampler));
    EXPECT_NEAR(s.timeUntilNextFire, 5.0f, kEps);
}

TEST(RandomOneShot, DrawsFreshIntervalEachFireUsingSamplerSequence)
{
    // Simulates a deterministic sequence of uniform samples — the
    // same as calling std::uniform_real_distribution with a fixed
    // seed. Each fire pulls the next value from the queue.
    std::queue<float> samples;
    samples.push(0.0f);
    samples.push(0.5f);
    samples.push(1.0f);

    auto sampler = [&samples]()
    {
        if (samples.empty()) return 0.5f;
        const float v = samples.front();
        samples.pop();
        return v;
    };

    RandomOneShotScheduler s;
    s.minIntervalSeconds = 10.0f;
    s.maxIntervalSeconds = 20.0f;
    s.timeUntilNextFire  = 0.0f;

    EXPECT_TRUE(tickRandomOneShot(s, 0.0f, sampler));
    EXPECT_NEAR(s.timeUntilNextFire, 10.0f, kEps);

    s.timeUntilNextFire = 0.0f;
    EXPECT_TRUE(tickRandomOneShot(s, 0.0f, sampler));
    EXPECT_NEAR(s.timeUntilNextFire, 15.0f, kEps);

    s.timeUntilNextFire = 0.0f;
    EXPECT_TRUE(tickRandomOneShot(s, 0.0f, sampler));
    EXPECT_NEAR(s.timeUntilNextFire, 20.0f, kEps);
}
