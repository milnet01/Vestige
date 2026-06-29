// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_lod.cpp
/// @brief Phase 10 audio quick-wins (AX5) — coverage for the pure audio
///        level-of-detail tier decision: the distance ladder, hysteresis
///        dead-band, the Critical-priority floor, and the occlusion
///        coupling. No AL device needed.

#include <gtest/gtest.h>

#include "audio/audio_lod.h"
#include "audio/audio_mixer.h"  // SoundPriority

using namespace Vestige;

namespace
{
// Default-config helper: maxDistance 100 m so distance == ratio × 100.
AudioLodTier tierAt(float ratio, float occlusion = 0.0f,
                    SoundPriority priority = SoundPriority::Normal,
                    AudioLodTier previous = AudioLodTier::Full)
{
    AudioLodConfig cfg;  // cheap 0.6, drop2D 0.85, mute 1.0, hysteresis 0.05
    return audioLodTier(ratio * 100.0f, 100.0f, occlusion, priority,
                        previous, cfg);
}
}

// -- Disable switch --------------------------------------------------

TEST(AudioLod, DisabledAlwaysFull)
{
    AudioLodConfig cfg;
    cfg.enabled = false;
    EXPECT_EQ(audioLodTier(1000.0f, 100.0f, 1.0f, SoundPriority::Low,
                           AudioLodTier::Mute, cfg),
              AudioLodTier::Full);
}

TEST(AudioLod, ZeroMaxDistanceIsFull)
{
    AudioLodConfig cfg;
    EXPECT_EQ(audioLodTier(50.0f, 0.0f, 0.0f, SoundPriority::Normal,
                           AudioLodTier::Full, cfg),
              AudioLodTier::Full);
}

// -- Distance ladder (from Full, so boundaries carry the +h bias) ----

TEST(AudioLod, DistanceLadderProgresses)
{
    EXPECT_EQ(tierAt(0.50f), AudioLodTier::Full);          // < 0.65
    EXPECT_EQ(tierAt(0.70f), AudioLodTier::CheapSpatial);  // ≥ 0.65, < 0.90
    EXPECT_EQ(tierAt(0.95f), AudioLodTier::Drop2D);        // ≥ 0.90, < 1.05
    EXPECT_EQ(tierAt(1.10f), AudioLodTier::Mute);          // ≥ 1.05
}

TEST(AudioLod, MonotonicDemotionWithDistance)
{
    int prev = 0;
    for (float r = 0.0f; r <= 1.3f; r += 0.05f)
    {
        const int t = static_cast<int>(tierAt(r));
        EXPECT_GE(t, prev);  // tier index never decreases as distance grows
        prev = t;
    }
}

// -- Hysteresis dead-band --------------------------------------------

TEST(AudioLod, HysteresisKeepsPreviousTierInsideDeadBand)
{
    // ratio 0.62 sits between the nominal cheap boundary (0.60) and the
    // +h entry threshold (0.65). A source previously Full stays Full;
    // one previously CheapSpatial stays CheapSpatial. The dead-band
    // stops it flapping.
    EXPECT_EQ(tierAt(0.62f, 0.0f, SoundPriority::Normal, AudioLodTier::Full),
              AudioLodTier::Full);
    EXPECT_EQ(tierAt(0.62f, 0.0f, SoundPriority::Normal,
                     AudioLodTier::CheapSpatial),
              AudioLodTier::CheapSpatial);
}

// -- Critical-priority floor -----------------------------------------

TEST(AudioLod, CriticalNeverBelowCheapSpatial)
{
    // A far Critical source (would be Mute) clamps up to CheapSpatial so
    // dialogue keeps its 3D position.
    EXPECT_EQ(tierAt(1.20f, 0.0f, SoundPriority::Critical),
              AudioLodTier::CheapSpatial);
    EXPECT_EQ(tierAt(0.95f, 0.0f, SoundPriority::Critical),
              AudioLodTier::CheapSpatial);  // Drop2D clamped up
    // But a near Critical source is still Full (the clamp is a floor on
    // cheapness, not a forced demotion).
    EXPECT_EQ(tierAt(0.10f, 0.0f, SoundPriority::Critical),
              AudioLodTier::Full);
}

// -- Occlusion coupling ----------------------------------------------

TEST(AudioLod, HeavyOcclusionDemotesNearSource)
{
    // A near source (ratio 0.1) that is 70% occluded is already near-
    // silent on the gain path, so the ladder demotes it to CheapSpatial
    // via the effective max(distance, occlusion) ratio.
    EXPECT_EQ(tierAt(0.10f, 0.70f), AudioLodTier::CheapSpatial);
    // Distance still dominates when it is the larger term.
    EXPECT_EQ(tierAt(0.95f, 0.10f), AudioLodTier::Drop2D);
}
