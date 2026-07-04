// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_reverb_zone_select.cpp
/// @brief AX2 R3 — the pure reverb-zone selection + blend + slew that
///        `ReverbSystem` drives the aux slot from. Device-free (no glm, no
///        OpenAL): these pin the "which room wins and how do two rooms
///        crossfade" math the headless AudioEngine suite cannot reach, plus a
///        Release-gated micro-benchmark for the per-frame selection cost
///        (design § 7: ≤ 0.05 ms main-thread).

#include <gtest/gtest.h>

#include "audio/audio_reverb.h"

#include <algorithm>
#include <chrono>
#include <vector>

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;

ReverbZoneEval evalAt(float distance, float core, float band,
                      const ReverbParams& params, float wet)
{
    ReverbZoneEval e;
    e.distance    = distance;
    e.coreRadius  = core;
    e.falloffBand = band;
    e.params      = params;
    e.wetGain     = wet;
    return e;
}
}  // namespace

// -- No winner → dry -----------------------------------------------------------

TEST(ReverbZoneSelect, NoZonesReturnsDry)
{
    const ReverbSelection sel = selectReverbZone({});
    EXPECT_EQ(sel.winner, -1);
    EXPECT_EQ(sel.neighbour, -1);
    EXPECT_NEAR(sel.targetWetGain, 0.0f, kEps);
    EXPECT_NEAR(sel.winnerWeight, 0.0f, kEps);
}

TEST(ReverbZoneSelect, ListenerOutsideEveryZoneIsDry)
{
    // core 0, band 10 → weight 0 beyond distance 10. Listener at 50 m.
    std::vector<ReverbZoneEval> zones{
        evalAt(50.0f, 0.0f, 10.0f, reverbPresetParams(ReverbPreset::Cave), 0.5f)};
    const ReverbSelection sel = selectReverbZone(zones);
    EXPECT_EQ(sel.winner, -1);
    EXPECT_NEAR(sel.targetWetGain, 0.0f, kEps);
}

// -- Single zone: depth scales the wet gain -----------------------------------

TEST(ReverbZoneSelect, SingleZoneCoreIsFullWet)
{
    // Listener inside the core (distance ≤ core) → weight 1 → full wet.
    std::vector<ReverbZoneEval> zones{
        evalAt(3.0f, 5.0f, 2.0f, reverbPresetParams(ReverbPreset::LargeHall), 0.4f)};
    const ReverbSelection sel = selectReverbZone(zones);
    ASSERT_EQ(sel.winner, 0);
    EXPECT_EQ(sel.neighbour, -1);
    EXPECT_NEAR(sel.blendT, 0.0f, kEps);
    EXPECT_NEAR(sel.winnerWeight, 1.0f, kEps);
    EXPECT_NEAR(sel.targetWetGain, 0.4f, kEps);            // 0.4 * 1.0
    // No neighbour → the character is the winner's own params, untouched.
    EXPECT_EQ(sel.blendedParams, reverbPresetParams(ReverbPreset::LargeHall));
}

TEST(ReverbZoneSelect, SingleZoneFalloffScalesWetByDepth)
{
    // core 0, band 10, distance 5 → weight 0.5 → half wet (fades at the edge).
    std::vector<ReverbZoneEval> zones{
        evalAt(5.0f, 0.0f, 10.0f, reverbPresetParams(ReverbPreset::Generic), 0.6f)};
    const ReverbSelection sel = selectReverbZone(zones);
    ASSERT_EQ(sel.winner, 0);
    EXPECT_NEAR(sel.winnerWeight, 0.5f, kEps);
    EXPECT_NEAR(sel.targetWetGain, 0.30f, kEps);           // 0.6 * 0.5
}

// -- Two zones: winner = highest weight, neighbour = second -------------------

TEST(ReverbZoneSelect, WinnerIsTheHighestWeightedZone)
{
    ReverbParams a = reverbPresetParams(ReverbPreset::SmallRoom);
    ReverbParams b = reverbPresetParams(ReverbPreset::Cave);
    // core 0, band 10: zone0 at 2 → weight 0.8; zone1 at 5 → weight 0.5.
    std::vector<ReverbZoneEval> zones{
        evalAt(2.0f, 0.0f, 10.0f, a, 0.3f),
        evalAt(5.0f, 0.0f, 10.0f, b, 0.9f)};
    const ReverbSelection sel = selectReverbZone(zones);
    EXPECT_EQ(sel.winner, 0);
    EXPECT_EQ(sel.neighbour, 1);
    EXPECT_NEAR(sel.winnerWeight, 0.8f, kEps);
    EXPECT_GT(sel.blendT, 0.0f);
    EXPECT_LE(sel.blendT, 0.5f);   // winner always weighs ≥ neighbour.
}

TEST(ReverbZoneSelect, DoorwayBlendsParamsAndGainAtMidpoint)
{
    // Two equal-weight zones (both at distance 5, core 0, band 10 → weight 0.5).
    // Distinctive params so the blend is observable.
    ReverbParams a;  a.decayTime = 1.0f;
    ReverbParams b;  b.decayTime = 5.0f;
    std::vector<ReverbZoneEval> zones{
        evalAt(5.0f, 0.0f, 10.0f, a, 0.4f),
        evalAt(5.0f, 0.0f, 10.0f, b, 0.8f)};
    const ReverbSelection sel = selectReverbZone(zones);
    ASSERT_EQ(sel.winner, 0);        // tie → lower index wins
    ASSERT_EQ(sel.neighbour, 1);
    EXPECT_NEAR(sel.blendT, 0.5f, kEps);
    EXPECT_NEAR(sel.blendedParams.decayTime, 3.0f, kEps);          // midway
    // (0.4*0.5 + 0.8*0.5) * winnerWeight(0.5) = 0.6 * 0.5 = 0.30.
    EXPECT_NEAR(sel.targetWetGain, 0.30f, kEps);
}

TEST(ReverbZoneSelect, TieResolvesToLowerIndex)
{
    ReverbParams p = reverbPresetParams(ReverbPreset::Generic);
    std::vector<ReverbZoneEval> zones{
        evalAt(0.0f, 5.0f, 2.0f, p, 0.5f),    // weight 1
        evalAt(0.0f, 5.0f, 2.0f, p, 0.5f)};   // weight 1 (tie)
    const ReverbSelection sel = selectReverbZone(zones);
    EXPECT_EQ(sel.winner, 0);
    EXPECT_EQ(sel.neighbour, 1);
}

TEST(ReverbZoneSelect, ZeroWeightZonesAreIgnoredAsNeighbour)
{
    ReverbParams p = reverbPresetParams(ReverbPreset::Generic);
    // Winner in range; the second zone is far out of range (weight 0).
    std::vector<ReverbZoneEval> zones{
        evalAt(1.0f, 5.0f, 2.0f, p, 0.5f),    // weight 1
        evalAt(99.0f, 5.0f, 2.0f, p, 0.5f)};  // weight 0 → not a neighbour
    const ReverbSelection sel = selectReverbZone(zones);
    EXPECT_EQ(sel.winner, 0);
    EXPECT_EQ(sel.neighbour, -1);
    EXPECT_NEAR(sel.blendT, 0.0f, kEps);
}

// -- Wet-gain slew -------------------------------------------------------------

TEST(ReverbZoneSelect, SlewMovesFractionallyTowardTarget)
{
    EXPECT_NEAR(slewReverbWetGain(0.0f, 1.0f, 0.25f), 0.25f, kEps);
    EXPECT_NEAR(slewReverbWetGain(0.4f, 0.0f, 0.5f),  0.2f,  kEps);
}

TEST(ReverbZoneSelect, SlewSnapsAtOneAndClampsAmount)
{
    EXPECT_NEAR(slewReverbWetGain(0.2f, 0.9f, 1.0f),  0.9f, kEps);   // snap
    EXPECT_NEAR(slewReverbWetGain(0.2f, 0.9f, 5.0f),  0.9f, kEps);   // clamp >1
    EXPECT_NEAR(slewReverbWetGain(0.2f, 0.9f, -1.0f), 0.2f, kEps);   // clamp <0
}

// -- Release-gated micro-benchmark (design § 7: ≤ 0.05 ms / frame) ------------

TEST(ReverbZoneSelect, SelectionUnderPerFrameBudget)
{
    // A generous 32-zone scene (reverb is per-room; real scenes have far fewer).
    // core 0, band 20; distances fan out so several overlap the "listener".
    std::vector<ReverbZoneEval> zones;
    zones.reserve(32);
    for (int i = 0; i < 32; ++i)
    {
        zones.push_back(evalAt(static_cast<float>(i) * 0.7f, 0.0f, 20.0f,
                               reverbPresetParams(static_cast<ReverbPreset>(i % 6)),
                               0.3f));
    }

    auto timeOnce = [&]() -> double
    {
        const auto t0 = std::chrono::steady_clock::now();
        const ReverbSelection sel = selectReverbZone(zones);
        const auto t1 = std::chrono::steady_clock::now();
        // Consume the result so the call is not optimised away.
        EXPECT_GE(sel.winner, 0);
        return std::chrono::duration<double, std::micro>(t1 - t0).count();
    };

    for (int i = 0; i < 4; ++i) timeOnce();  // warm the cache / branch predictor

    constexpr int kRuns = 16;
    std::vector<double> micros;
    micros.reserve(kRuns);
    for (int i = 0; i < kRuns; ++i) micros.push_back(timeOnce());
    std::sort(micros.begin(), micros.end());
    const double medianMicros = micros[micros.size() / 2];

    // design § 7: ≤ 0.05 ms = 50 µs main-thread per frame.
    constexpr double kBudgetMicros = 50.0;

#if !defined(NDEBUG)
    // Debug (-O0) runs this several× slower than the optimised build the budget
    // targets, so the wall-clock is not comparable — exercise the path (proving
    // it does not crash) but skip the gate. Enforced in Release.
    GTEST_SKIP() << "non-optimised (Debug) build — reverb-zone selection median "
                 << medianMicros << " µs not gated against the " << kBudgetMicros
                 << " µs budget (enforced in optimised builds).";
#else
    EXPECT_LE(medianMicros, kBudgetMicros)
        << "reverb-zone selection over 32 zones median " << medianMicros
        << " µs exceeds the " << kBudgetMicros << " µs / frame budget.";
#endif
}
