// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_air_absorption.cpp
/// @brief Phase 10 audio quick-wins (AX6) — coverage for the headless
///        ISO 9613-1 air-absorption curve: unity at the near field,
///        the disable switch, monotonicity + [0,1] bound, humidity /
///        temperature endpoints, and a regression anchor against the
///        closed-form ISO value at standard conditions.
///
/// The reference gains were computed directly from the ISO 9613-1
/// closed form (4 kHz anchor) — the runtime curve IS that closed form,
/// so this pins the implementation against the standard rather than a
/// separate Workbench fit. A future fitted approximation (the
/// `TODO: revisit via Formula Workbench` in the header) would be
/// parity-tested against these same anchors.

#include <gtest/gtest.h>

#include "audio/audio_air_absorption.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;
}

// -- Near field & disable switch ------------------------------------

TEST(AudioAirAbsorption, UnityAtZeroDistance)
{
    AirAbsorptionParams p;  // 20 °C, 50% RH, enabled
    EXPECT_NEAR(airAbsorptionHfGain(0.0f, p),  1.0f, kEps);
    EXPECT_NEAR(airAbsorptionHfGain(-5.0f, p), 1.0f, kEps);  // clamps to unity
}

TEST(AudioAirAbsorption, DisabledReturnsUnity)
{
    AirAbsorptionParams p;
    p.enabled = false;
    EXPECT_NEAR(airAbsorptionHfGain(10.0f,   p), 1.0f, kEps);
    EXPECT_NEAR(airAbsorptionHfGain(1000.0f, p), 1.0f, kEps);
}

// -- Shape: monotonic non-increasing, bounded [0,1] -----------------

TEST(AudioAirAbsorption, MonotonicNonIncreasingInDistance)
{
    AirAbsorptionParams p;
    float prev = 1.0f;
    for (float d = 0.0f; d <= 500.0f; d += 10.0f)
    {
        const float g = airAbsorptionHfGain(d, p);
        EXPECT_LE(g, prev + kEps);  // never rises with distance
        prev = g;
    }
}

TEST(AudioAirAbsorption, BoundedZeroToOne)
{
    // Sweep the full ISO validity range (T ∈ [-20,50] °C, h ∈ [10,100] %)
    // at a long distance and assert the gain stays a valid multiplier.
    for (float Tc = -20.0f; Tc <= 50.0f; Tc += 5.0f)
    {
        for (float h = 0.1f; h <= 1.0f; h += 0.1f)
        {
            AirAbsorptionParams p;
            p.temperatureC = Tc;
            p.humidity01   = h;
            const float g = airAbsorptionHfGain(2000.0f, p);
            EXPECT_GE(g, 0.0f);
            EXPECT_LE(g, 1.0f);
        }
    }
}

// -- Regression anchors against the ISO 9613-1 closed form ----------

TEST(AudioAirAbsorption, ReferenceMatchesIsoAtStandardConditions)
{
    AirAbsorptionParams p;  // 20 °C, 50% RH
    // 4 kHz, 100 m, sea level → ~2.97 dB loss → gain 0.7107.
    EXPECT_NEAR(airAbsorptionHfGain(100.0f, p), 0.71068f, 1e-3f);
    // Same at 50 m → ~1.48 dB → gain 0.8430.
    EXPECT_NEAR(airAbsorptionHfGain(50.0f,  p), 0.84302f, 1e-3f);
}

TEST(AudioAirAbsorption, DrierAirAbsorbsMoreHfAt4kHz)
{
    // At 4 kHz the absorption peak sits in dry air, so 20% RH should
    // muffle a distant source MORE (lower gain) than 80% RH — the
    // physically-correct, if counter-intuitive, ISO behaviour.
    AirAbsorptionParams dry;  dry.humidity01  = 0.2f;
    AirAbsorptionParams damp; damp.humidity01 = 0.8f;
    EXPECT_LT(airAbsorptionHfGain(100.0f, dry),
              airAbsorptionHfGain(100.0f, damp));
}

TEST(AudioAirAbsorption, TemperatureChangesGain)
{
    // Endpoint sanity: cold air (0 °C) absorbs HF more than the 20 °C
    // reference at 50% RH over 100 m (ISO α rises as T drops here).
    AirAbsorptionParams cold; cold.temperatureC = 0.0f;
    AirAbsorptionParams warm; warm.temperatureC = 20.0f;
    EXPECT_LT(airAbsorptionHfGain(100.0f, cold),
              airAbsorptionHfGain(100.0f, warm));
}
