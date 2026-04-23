// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_mixer.cpp
/// @brief Phase 10 coverage for mixer bus gains, ducking state
///        machine, and priority-based voice eviction.

#include <gtest/gtest.h>

#include "audio/audio_mixer.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;
}

// -- Labels --------------------------------------------------------

TEST(AudioMixer, PriorityLabelsAreStable)
{
    EXPECT_STREQ(soundPriorityLabel(SoundPriority::Low),      "Low");
    EXPECT_STREQ(soundPriorityLabel(SoundPriority::Normal),   "Normal");
    EXPECT_STREQ(soundPriorityLabel(SoundPriority::High),     "High");
    EXPECT_STREQ(soundPriorityLabel(SoundPriority::Critical), "Critical");
}

TEST(AudioMixer, PriorityRanksAreMonotonic)
{
    EXPECT_LT(soundPriorityRank(SoundPriority::Low),      soundPriorityRank(SoundPriority::Normal));
    EXPECT_LT(soundPriorityRank(SoundPriority::Normal),   soundPriorityRank(SoundPriority::High));
    EXPECT_LT(soundPriorityRank(SoundPriority::High),     soundPriorityRank(SoundPriority::Critical));
}

TEST(AudioMixer, BusLabelsAreStable)
{
    EXPECT_STREQ(audioBusLabel(AudioBus::Master),  "Master");
    EXPECT_STREQ(audioBusLabel(AudioBus::Music),   "Music");
    EXPECT_STREQ(audioBusLabel(AudioBus::Voice),   "Voice");
    EXPECT_STREQ(audioBusLabel(AudioBus::Sfx),     "Sfx");
    EXPECT_STREQ(audioBusLabel(AudioBus::Ambient), "Ambient");
    EXPECT_STREQ(audioBusLabel(AudioBus::Ui),      "Ui");
}

// -- Mixer bus gains ----------------------------------------------

TEST(AudioMixer, DefaultsToUnityPerBus)
{
    AudioMixer m;
    for (auto bus : {AudioBus::Master, AudioBus::Music, AudioBus::Voice,
                      AudioBus::Sfx, AudioBus::Ambient, AudioBus::Ui})
    {
        EXPECT_NEAR(effectiveBusGain(m, bus), 1.0f, kEps);
    }
}

TEST(AudioMixer, EffectiveGainMultipliesWithMaster)
{
    AudioMixer m;
    m.busGain[static_cast<std::size_t>(AudioBus::Master)] = 0.5f;
    m.busGain[static_cast<std::size_t>(AudioBus::Music)]  = 0.4f;
    EXPECT_NEAR(effectiveBusGain(m, AudioBus::Music), 0.2f, kEps);
}

TEST(AudioMixer, MasterBusIgnoresSelfDouble)
{
    // Querying Master returns just the Master gain (not Master*Master).
    AudioMixer m;
    m.busGain[static_cast<std::size_t>(AudioBus::Master)] = 0.5f;
    EXPECT_NEAR(effectiveBusGain(m, AudioBus::Master), 0.5f, kEps);
}

TEST(AudioMixer, EffectiveGainClampsToUnitRange)
{
    AudioMixer m;
    m.busGain[static_cast<std::size_t>(AudioBus::Master)] = 2.0f;
    m.busGain[static_cast<std::size_t>(AudioBus::Music)]  = 2.0f;
    EXPECT_NEAR(effectiveBusGain(m, AudioBus::Music), 1.0f, kEps);

    m.busGain[static_cast<std::size_t>(AudioBus::Master)] = -0.5f;
    m.busGain[static_cast<std::size_t>(AudioBus::Music)]  = 0.5f;
    EXPECT_NEAR(effectiveBusGain(m, AudioBus::Music), 0.0f, kEps);
}

// -- Phase 10.7 slice A2: resolveSourceGain -----------------------

TEST(AudioMixerResolve, DefaultMixerWithUnityVolumeYieldsUnity)
{
    AudioMixer m;
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Sfx, 1.0f), 1.0f, kEps);
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Music, 1.0f), 1.0f, kEps);
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Master, 1.0f), 1.0f, kEps);
}

TEST(AudioMixerResolve, ComposesMasterTimesBusTimesSource)
{
    AudioMixer m;
    m.setBusGain(AudioBus::Master, 0.5f);
    m.setBusGain(AudioBus::Music,  0.8f);
    // master × bus × vol = 0.5 × 0.8 × 0.75 = 0.30
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Music, 0.75f), 0.30f, kEps);
}

TEST(AudioMixerResolve, ClampsNegativeSourceVolumeToZero)
{
    AudioMixer m;
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Sfx, -0.5f), 0.0f, kEps);
}

TEST(AudioMixerResolve, ClampsAboveUnitySourceVolumeToOne)
{
    AudioMixer m;
    // master=1, bus=1, vol=1.5 — the pre-multiplication clamp on
    // volume means the composed gain stays at 1.0 rather than 1.5.
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Sfx, 1.5f), 1.0f, kEps);
}

TEST(AudioMixerResolve, MasterBusMultipliesOnlyOnce)
{
    // A source assigned directly to the Master bus (e.g. a
    // system-wide notification blast) should not double-apply the
    // master gain.
    AudioMixer m;
    m.setBusGain(AudioBus::Master, 0.4f);
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Master, 1.0f), 0.4f, kEps);
}

TEST(AudioMixerResolve, ZeroMasterSilencesEverything)
{
    AudioMixer m;
    m.setBusGain(AudioBus::Master, 0.0f);
    m.setBusGain(AudioBus::Music,  1.0f);
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Music, 1.0f), 0.0f, kEps);
}

TEST(AudioMixerResolve, ZeroBusSilencesOnlyThatBus)
{
    AudioMixer m;
    m.setBusGain(AudioBus::Music, 0.0f);
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Music, 1.0f), 0.0f, kEps);
    // Sfx still plays at full gain.
    EXPECT_NEAR(resolveSourceGain(m, AudioBus::Sfx,   1.0f), 1.0f, kEps);
}

// -- Ducking -------------------------------------------------------

TEST(AudioDucking, AttacksTowardFloorWhenTriggered)
{
    DuckingParams params;
    params.attackSeconds  = 0.1f;
    params.releaseSeconds = 0.3f;
    params.duckFactor     = 0.4f;

    DuckingState s;
    s.currentGain = 1.0f;
    s.triggered   = true;

    // After 0.05s (half the attack window) we should be halfway.
    updateDucking(s, params, 0.05f);
    EXPECT_NEAR(s.currentGain, 0.7f, kEps);  // 1.0 - 0.5 * (1.0 - 0.4)

    // After another 0.05s we hit the floor.
    updateDucking(s, params, 0.05f);
    EXPECT_NEAR(s.currentGain, 0.4f, kEps);

    // Further ticks hold.
    updateDucking(s, params, 1.0f);
    EXPECT_NEAR(s.currentGain, 0.4f, kEps);
}

TEST(AudioDucking, ReleasesTowardUnityWhenTriggerFalls)
{
    DuckingParams params;
    params.attackSeconds  = 0.1f;
    params.releaseSeconds = 0.2f;
    params.duckFactor     = 0.4f;

    DuckingState s;
    s.currentGain = 0.4f;
    s.triggered   = false;

    // Release travel is 0.6 (=1.0-0.4) over 0.2s → 3.0 /s.
    updateDucking(s, params, 0.1f);
    EXPECT_NEAR(s.currentGain, 0.7f, kEps);

    updateDucking(s, params, 0.1f);
    EXPECT_NEAR(s.currentGain, 1.0f, kEps);
}

TEST(AudioDucking, NeverDropsBelowFloor)
{
    DuckingParams params;
    params.attackSeconds  = 0.001f;  // very fast
    params.duckFactor     = 0.4f;

    DuckingState s;
    s.currentGain = 0.4f;
    s.triggered   = true;

    // Huge dt shouldn't push us below floor.
    updateDucking(s, params, 10.0f);
    EXPECT_NEAR(s.currentGain, 0.4f, kEps);
}

TEST(AudioDucking, NeverExceedsUnity)
{
    DuckingParams params;
    params.releaseSeconds = 0.001f;
    params.duckFactor     = 0.4f;

    DuckingState s;
    s.currentGain = 1.0f;
    s.triggered   = false;

    updateDucking(s, params, 10.0f);
    EXPECT_NEAR(s.currentGain, 1.0f, kEps);
}

TEST(AudioDucking, NegativeDtIsNoOp)
{
    DuckingParams params;
    DuckingState s;
    s.currentGain = 0.5f;
    s.triggered   = true;
    updateDucking(s, params, -1.0f);
    EXPECT_NEAR(s.currentGain, 0.5f, kEps);
}

TEST(AudioDucking, ZeroDurationFallsBackToEpsilon)
{
    // Setting a zero attack shouldn't divide by zero — the impl
    // should clamp with an epsilon so we still slew (essentially
    // instantly) rather than producing inf/nan.
    DuckingParams params;
    params.attackSeconds = 0.0f;
    params.duckFactor    = 0.4f;

    DuckingState s;
    s.currentGain = 1.0f;
    s.triggered   = true;

    updateDucking(s, params, 0.001f);
    EXPECT_GE(s.currentGain, 0.4f);
    EXPECT_LE(s.currentGain, 1.0f);
}

// -- Voice eviction -----------------------------------------------

TEST(AudioEviction, EmptyListReturnsSentinel)
{
    std::vector<VoiceCandidate> none;
    EXPECT_EQ(chooseVoiceToEvict(none), static_cast<std::size_t>(-1));
}

TEST(AudioEviction, LowerPriorityEvictsBeforeHigher)
{
    std::vector<VoiceCandidate> voices;
    voices.push_back({SoundPriority::Critical, 1.0f, 0.0f});
    voices.push_back({SoundPriority::Low,      1.0f, 0.0f});
    voices.push_back({SoundPriority::High,     1.0f, 0.0f});
    EXPECT_EQ(chooseVoiceToEvict(voices), 1u);
}

TEST(AudioEviction, WithinPriorityTierQuieterGoesFirst)
{
    std::vector<VoiceCandidate> voices;
    voices.push_back({SoundPriority::Normal, 1.0f, 0.0f});
    voices.push_back({SoundPriority::Normal, 0.1f, 0.0f});  // quietest
    voices.push_back({SoundPriority::Normal, 0.5f, 0.0f});
    EXPECT_EQ(chooseVoiceToEvict(voices), 1u);
}

TEST(AudioEviction, WithinTierAndGainOldestGoesFirst)
{
    std::vector<VoiceCandidate> voices;
    voices.push_back({SoundPriority::Normal, 0.5f,  2.0f});
    voices.push_back({SoundPriority::Normal, 0.5f,  5.0f});  // oldest
    voices.push_back({SoundPriority::Normal, 0.5f,  1.0f});
    EXPECT_EQ(chooseVoiceToEvict(voices), 1u);
}

TEST(AudioEviction, CriticalSurvivesEvenIfVerySoftAndOld)
{
    // A loud Low can still be worth keeping over a silent Critical? No —
    // Critical priority dominates by 1000 units, which overwhelms any
    // combination of gain and age that fits in a realistic range.
    std::vector<VoiceCandidate> voices;
    voices.push_back({SoundPriority::Critical, 0.01f, 60.0f});  // very soft, 1 min old
    voices.push_back({SoundPriority::Low,       1.0f,   0.5f});  // loud & fresh
    EXPECT_EQ(chooseVoiceToEvict(voices), 1u);
}

TEST(AudioEviction, KeepScoreOrdering)
{
    // Sanity: voiceKeepScore should match the ordering implied by
    // the priority rank.
    VoiceCandidate low;      low.priority      = SoundPriority::Low;
    VoiceCandidate critical; critical.priority = SoundPriority::Critical;
    EXPECT_LT(voiceKeepScore(low), voiceKeepScore(critical));
}
