// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_panel.cpp
/// @brief Non-GL tests for the editor AudioPanel — open/close
///        toggle, zone add/select/remove, mute/solo sets, and the
///        effective-gain router for the AudioSystem integration
///        point.

#include <gtest/gtest.h>

#include "editor/panels/audio_panel.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;
}

// -- Defaults + open/close ----------------------------------------

TEST(AudioPanel, DefaultsAreClosed)
{
    AudioPanel p;
    EXPECT_FALSE(p.isOpen());
    EXPECT_FALSE(p.isZoneOverlayEnabled());
    EXPECT_EQ(p.selectedReverbZone(),  -1);
    EXPECT_EQ(p.selectedAmbientZone(), -1);
    EXPECT_TRUE(p.reverbZones().empty());
    EXPECT_TRUE(p.ambientZones().empty());
    EXPECT_FALSE(p.hasAnySoloedSource());
}

TEST(AudioPanel, SetOpenToggles)
{
    AudioPanel p;
    p.setOpen(true);
    EXPECT_TRUE(p.isOpen());
    p.toggleOpen();
    EXPECT_FALSE(p.isOpen());
    p.toggleOpen();
    EXPECT_TRUE(p.isOpen());
}

// -- Mixer + ducking defaults -------------------------------------

TEST(AudioPanel, MixerDefaultsToUnityGains)
{
    AudioPanel p;
    for (auto bus : {AudioBus::Master, AudioBus::Music, AudioBus::Voice,
                      AudioBus::Sfx, AudioBus::Ambient, AudioBus::Ui})
    {
        EXPECT_NEAR(effectiveBusGain(p.mixer(), bus), 1.0f, kEps);
    }
}

TEST(AudioPanel, DuckingStartsUntriggeredAtUnity)
{
    AudioPanel p;
    EXPECT_FALSE(p.duckingState().triggered);
    EXPECT_NEAR(p.duckingState().currentGain, 1.0f, kEps);
}

// -- Reverb zone management ---------------------------------------

TEST(AudioPanel, AddReverbZoneReturnsIndex)
{
    AudioPanel p;
    AudioPanel::ReverbZoneInstance z;
    z.name = "Cave";
    EXPECT_EQ(p.addReverbZone(z), 0);
    EXPECT_EQ(p.reverbZones().size(), 1u);
    EXPECT_EQ(p.reverbZones()[0].name, "Cave");

    AudioPanel::ReverbZoneInstance z2;
    z2.name = "Hall";
    EXPECT_EQ(p.addReverbZone(z2), 1);
    EXPECT_EQ(p.reverbZones().size(), 2u);
}

TEST(AudioPanel, RemoveReverbZoneShiftsSelectionDown)
{
    AudioPanel p;
    p.addReverbZone({});
    p.addReverbZone({});
    p.addReverbZone({});
    p.selectReverbZone(2);

    // Remove index 0 — selection now points at what was index 2,
    // which is now index 1.
    EXPECT_TRUE(p.removeReverbZone(0));
    EXPECT_EQ(p.selectedReverbZone(), 1);
    EXPECT_EQ(p.reverbZones().size(), 2u);
}

TEST(AudioPanel, RemoveSelectedReverbZoneClearsSelection)
{
    AudioPanel p;
    p.addReverbZone({});
    p.selectReverbZone(0);
    EXPECT_TRUE(p.removeReverbZone(0));
    EXPECT_EQ(p.selectedReverbZone(), -1);
}

TEST(AudioPanel, RemoveOutOfRangeReverbZoneIsNoOp)
{
    AudioPanel p;
    p.addReverbZone({});
    EXPECT_FALSE(p.removeReverbZone(-1));
    EXPECT_FALSE(p.removeReverbZone(5));
    EXPECT_EQ(p.reverbZones().size(), 1u);
}

// -- Ambient zone management --------------------------------------

TEST(AudioPanel, AddAmbientZoneReturnsIndex)
{
    AudioPanel p;
    AudioPanel::AmbientZoneInstance z;
    z.name = "Wind";
    EXPECT_EQ(p.addAmbientZone(z), 0);
    EXPECT_EQ(p.ambientZones().size(), 1u);
}

TEST(AudioPanel, RemoveAmbientZoneMirrorsReverbBehavior)
{
    AudioPanel p;
    p.addAmbientZone({});
    p.addAmbientZone({});
    p.selectAmbientZone(1);

    EXPECT_TRUE(p.removeAmbientZone(1));
    EXPECT_EQ(p.selectedAmbientZone(), -1);
    EXPECT_EQ(p.ambientZones().size(), 1u);
}

// -- Mute / solo --------------------------------------------------

TEST(AudioPanel, MuteTogglesSourceState)
{
    AudioPanel p;
    EXPECT_FALSE(p.isSourceMuted(42));
    p.setSourceMuted(42, true);
    EXPECT_TRUE(p.isSourceMuted(42));
    p.setSourceMuted(42, false);
    EXPECT_FALSE(p.isSourceMuted(42));
}

TEST(AudioPanel, SoloTogglesSourceState)
{
    AudioPanel p;
    EXPECT_FALSE(p.hasAnySoloedSource());
    p.setSourceSoloed(7, true);
    EXPECT_TRUE(p.isSourceSoloed(7));
    EXPECT_TRUE(p.hasAnySoloedSource());
    p.setSourceSoloed(7, false);
    EXPECT_FALSE(p.hasAnySoloedSource());
}

// -- Effective gain routing ---------------------------------------

TEST(AudioPanel, MutedSourceEffectiveGainIsZero)
{
    AudioPanel p;
    p.setSourceMuted(42, true);
    EXPECT_NEAR(p.computeEffectiveSourceGain(42, AudioBus::Sfx), 0.0f, kEps);
}

TEST(AudioPanel, SoloedSourcesOnlyPlay)
{
    AudioPanel p;
    p.setSourceSoloed(7, true);

    // 7 is soloed → audible.
    EXPECT_NEAR(p.computeEffectiveSourceGain(7, AudioBus::Music), 1.0f, kEps);
    // 8 is not soloed and at least one source is → silent.
    EXPECT_NEAR(p.computeEffectiveSourceGain(8, AudioBus::Music), 0.0f, kEps);
}

TEST(AudioPanel, MutedBeatsSolo)
{
    // If a source is both muted and soloed, mute wins (convention
    // matches every DAW — mute is the harder kill switch).
    AudioPanel p;
    p.setSourceSoloed(7, true);
    p.setSourceMuted(7, true);
    EXPECT_NEAR(p.computeEffectiveSourceGain(7, AudioBus::Music), 0.0f, kEps);
}

TEST(AudioPanel, EffectiveGainAppliesBusAndDucking)
{
    AudioPanel p;
    p.mixer().busGain[static_cast<std::size_t>(AudioBus::Master)] = 0.5f;
    p.mixer().busGain[static_cast<std::size_t>(AudioBus::Music)]  = 0.4f;
    p.duckingState().currentGain = 0.5f;
    // Master*Music*Duck = 0.5 * 0.4 * 0.5 = 0.1
    EXPECT_NEAR(p.computeEffectiveSourceGain(1, AudioBus::Music), 0.1f, kEps);
}

TEST(AudioPanel, EffectiveGainClampedToUnitRange)
{
    AudioPanel p;
    p.mixer().busGain[static_cast<std::size_t>(AudioBus::Master)] = 2.0f;
    p.mixer().busGain[static_cast<std::size_t>(AudioBus::Music)]  = 2.0f;
    p.duckingState().currentGain = 2.0f;
    EXPECT_NEAR(p.computeEffectiveSourceGain(1, AudioBus::Music), 1.0f, kEps);
}

// -- Debug overlay toggle -----------------------------------------

TEST(AudioPanel, ZoneOverlayToggle)
{
    AudioPanel p;
    EXPECT_FALSE(p.isZoneOverlayEnabled());
    p.setZoneOverlayEnabled(true);
    EXPECT_TRUE(p.isZoneOverlayEnabled());
}
