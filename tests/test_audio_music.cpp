// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_music.cpp
/// @brief Phase 10 coverage for dynamic music primitives — layer
///        slew state machine, intensity-to-layer-weights mapper,
///        and the stinger queue.

#include <gtest/gtest.h>

#include "audio/audio_music.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;
}

// -- Layer labels --------------------------------------------------

TEST(MusicLayer, LabelsAreStable)
{
    EXPECT_STREQ(musicLayerLabel(MusicLayer::Ambient),     "Ambient");
    EXPECT_STREQ(musicLayerLabel(MusicLayer::Tension),     "Tension");
    EXPECT_STREQ(musicLayerLabel(MusicLayer::Exploration), "Exploration");
    EXPECT_STREQ(musicLayerLabel(MusicLayer::Combat),      "Combat");
    EXPECT_STREQ(musicLayerLabel(MusicLayer::Discovery),   "Discovery");
    EXPECT_STREQ(musicLayerLabel(MusicLayer::Danger),      "Danger");
}

// -- advanceMusicLayer --------------------------------------------

TEST(MusicLayerState, ReachesTargetAfterSufficientTime)
{
    MusicLayerState s;
    s.currentGain        = 0.0f;
    s.targetGain         = 1.0f;
    s.fadeSpeedPerSecond = 0.5f;  // full swing in 2 seconds

    advanceMusicLayer(s, 1.0f);
    EXPECT_NEAR(s.currentGain, 0.5f, kEps);

    advanceMusicLayer(s, 1.0f);
    EXPECT_NEAR(s.currentGain, 1.0f, kEps);

    // Further ticks hold at target.
    advanceMusicLayer(s, 1.0f);
    EXPECT_NEAR(s.currentGain, 1.0f, kEps);
}

TEST(MusicLayerState, DoesNotOvershoot)
{
    MusicLayerState s;
    s.currentGain        = 0.0f;
    s.targetGain         = 1.0f;
    s.fadeSpeedPerSecond = 10.0f;  // large enough to overshoot in 1 step

    advanceMusicLayer(s, 1.0f);
    EXPECT_NEAR(s.currentGain, 1.0f, kEps);
}

TEST(MusicLayerState, CanFadeDownAsWell)
{
    MusicLayerState s;
    s.currentGain        = 1.0f;
    s.targetGain         = 0.0f;
    s.fadeSpeedPerSecond = 1.0f;

    advanceMusicLayer(s, 0.5f);
    EXPECT_NEAR(s.currentGain, 0.5f, kEps);
    advanceMusicLayer(s, 0.5f);
    EXPECT_NEAR(s.currentGain, 0.0f, kEps);
}

TEST(MusicLayerState, ClampsCurrentGainToUnitRange)
{
    MusicLayerState s;
    s.currentGain        = 2.0f;   // caller wrote a bogus value
    s.targetGain         = 2.0f;
    s.fadeSpeedPerSecond = 0.0f;

    advanceMusicLayer(s, 0.016f);
    EXPECT_NEAR(s.currentGain, 1.0f, kEps);

    s.currentGain        = -1.0f;
    s.targetGain         = -1.0f;
    advanceMusicLayer(s, 0.016f);
    EXPECT_NEAR(s.currentGain, 0.0f, kEps);
}

TEST(MusicLayerState, ZeroDeltaKeepsCurrent)
{
    MusicLayerState s;
    s.currentGain = 0.3f;
    s.targetGain  = 1.0f;
    advanceMusicLayer(s, 0.0f);
    EXPECT_NEAR(s.currentGain, 0.3f, kEps);
}

// -- intensityToLayerWeights -------------------------------------

TEST(MusicIntensity, CalmIntensitySelectsAmbient)
{
    auto w = intensityToLayerWeights(0.0f);
    EXPECT_NEAR(w.weightOf(MusicLayer::Ambient),     1.0f, kEps);
    EXPECT_NEAR(w.weightOf(MusicLayer::Exploration), 0.0f, kEps);
    EXPECT_NEAR(w.weightOf(MusicLayer::Tension),     0.0f, kEps);
    EXPECT_NEAR(w.weightOf(MusicLayer::Combat),      0.0f, kEps);
    EXPECT_NEAR(w.weightOf(MusicLayer::Danger),      0.0f, kEps);
}

TEST(MusicIntensity, FullIntensitySelectsDanger)
{
    auto w = intensityToLayerWeights(1.0f);
    EXPECT_NEAR(w.weightOf(MusicLayer::Ambient),     0.0f, kEps);
    EXPECT_NEAR(w.weightOf(MusicLayer::Danger),      1.0f, kEps);
}

TEST(MusicIntensity, IntermediateIntensityBlends)
{
    // Halfway between Ambient (peak 0.0) and Exploration (peak 0.25)
    // the triangle envelopes meet at 0.5/0.5 — a genuine blend
    // rather than one layer hard-winning.
    auto w = intensityToLayerWeights(0.125f);
    EXPECT_NEAR(w.weightOf(MusicLayer::Ambient),     0.5f, kEps);
    EXPECT_NEAR(w.weightOf(MusicLayer::Exploration), 0.5f, kEps);
    EXPECT_NEAR(w.weightOf(MusicLayer::Tension),     0.0f, kEps);

    // At the Exploration peak itself, Ambient crosses zero and
    // Exploration owns the mix.
    auto peakExp = intensityToLayerWeights(0.25f);
    EXPECT_NEAR(peakExp.weightOf(MusicLayer::Exploration), 1.0f, kEps);
    EXPECT_NEAR(peakExp.weightOf(MusicLayer::Ambient),     0.0f, kEps);
}

TEST(MusicIntensity, TensionPeaksAtMiddle)
{
    auto w = intensityToLayerWeights(0.5f);
    EXPECT_NEAR(w.weightOf(MusicLayer::Tension), 1.0f, kEps);
}

TEST(MusicIntensity, CombatPeaksAtThreeQuarters)
{
    auto w = intensityToLayerWeights(0.75f);
    EXPECT_NEAR(w.weightOf(MusicLayer::Combat), 1.0f, kEps);
}

TEST(MusicIntensity, ClampsOutOfRange)
{
    auto wLo = intensityToLayerWeights(-0.5f);
    auto wHi = intensityToLayerWeights( 1.5f);
    EXPECT_NEAR(wLo.weightOf(MusicLayer::Ambient), 1.0f, kEps);
    EXPECT_NEAR(wHi.weightOf(MusicLayer::Danger),  1.0f, kEps);
}

TEST(MusicIntensity, SilenceScalesEveryLayerDown)
{
    auto full  = intensityToLayerWeights(0.5f, 0.0f);
    auto half  = intensityToLayerWeights(0.5f, 0.5f);
    auto quiet = intensityToLayerWeights(0.5f, 1.0f);

    for (std::size_t i = 0; i < MusicLayerCount; ++i)
    {
        EXPECT_NEAR(half.values[i], full.values[i] * 0.5f, kEps);
        EXPECT_NEAR(quiet.values[i], 0.0f, kEps);
    }
}

TEST(MusicIntensity, SilenceIsIndependentOfIntensityRouting)
{
    // Ratios between layers at the same intensity should be
    // preserved under silence — silence is a uniform multiplier.
    auto full = intensityToLayerWeights(0.5f, 0.0f);
    auto dim  = intensityToLayerWeights(0.5f, 0.4f);

    for (std::size_t i = 0; i < MusicLayerCount; ++i)
    {
        if (full.values[i] > 1e-4f)
        {
            EXPECT_NEAR(dim.values[i] / full.values[i], 0.6f, kEps);
        }
    }
}

// -- MusicStingerQueue --------------------------------------------

TEST(MusicStingerQueue, EnqueueAndFireAfterDelay)
{
    MusicStingerQueue q;
    MusicStinger s;
    s.clipPath     = "ding.wav";
    s.delaySeconds = 0.5f;
    q.enqueue(s);

    auto fired = q.advance(0.25f);
    EXPECT_TRUE(fired.empty());
    EXPECT_EQ(q.pending(), 1u);

    fired = q.advance(0.3f);  // cumulative 0.55s, past the 0.5s delay
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].clipPath, "ding.wav");
    EXPECT_EQ(q.pending(), 0u);
}

TEST(MusicStingerQueue, FiresMultipleInOrder)
{
    MusicStingerQueue q;

    MusicStinger a; a.clipPath = "a"; a.delaySeconds = 0.0f;
    MusicStinger b; b.clipPath = "b"; b.delaySeconds = 0.0f;
    MusicStinger c; c.clipPath = "c"; c.delaySeconds = 0.0f;
    q.enqueue(a);
    q.enqueue(b);
    q.enqueue(c);

    auto fired = q.advance(0.016f);
    ASSERT_EQ(fired.size(), 3u);
    EXPECT_EQ(fired[0].clipPath, "a");
    EXPECT_EQ(fired[1].clipPath, "b");
    EXPECT_EQ(fired[2].clipPath, "c");
}

TEST(MusicStingerQueue, EvictsOldestWhenCapacityReached)
{
    MusicStingerQueue q;
    q.setCapacity(2);

    MusicStinger a; a.clipPath = "a"; a.delaySeconds = 1.0f;
    MusicStinger b; b.clipPath = "b"; b.delaySeconds = 1.0f;
    MusicStinger c; c.clipPath = "c"; c.delaySeconds = 1.0f;
    q.enqueue(a);
    q.enqueue(b);
    q.enqueue(c);  // should evict 'a'

    auto fired = q.advance(1.5f);
    ASSERT_EQ(fired.size(), 2u);
    EXPECT_EQ(fired[0].clipPath, "b");
    EXPECT_EQ(fired[1].clipPath, "c");
}

TEST(MusicStingerQueue, SetCapacityTrimsInPlace)
{
    MusicStingerQueue q;
    MusicStinger a; a.clipPath = "a"; a.delaySeconds = 1.0f;
    MusicStinger b; b.clipPath = "b"; b.delaySeconds = 1.0f;
    MusicStinger c; c.clipPath = "c"; c.delaySeconds = 1.0f;
    q.enqueue(a);
    q.enqueue(b);
    q.enqueue(c);

    q.setCapacity(1);
    EXPECT_EQ(q.pending(), 1u);

    auto fired = q.advance(1.5f);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].clipPath, "c");
}

TEST(MusicStingerQueue, ZeroCapacityRejectsEnqueue)
{
    MusicStingerQueue q;
    q.setCapacity(0);
    MusicStinger a; a.clipPath = "a";
    q.enqueue(a);
    EXPECT_EQ(q.pending(), 0u);
}

TEST(MusicStingerQueue, ClearDropsPending)
{
    MusicStingerQueue q;
    MusicStinger a; a.delaySeconds = 1.0f;
    q.enqueue(a);
    q.enqueue(a);
    q.clear();
    EXPECT_EQ(q.pending(), 0u);
}

TEST(MusicStingerQueue, NegativeDeltaDoesNotCauseFire)
{
    MusicStingerQueue q;
    MusicStinger a; a.delaySeconds = 1.0f; a.clipPath = "a";
    q.enqueue(a);

    auto fired = q.advance(-5.0f);
    EXPECT_TRUE(fired.empty());
    EXPECT_EQ(q.pending(), 1u);
}
