// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_source_component.cpp
/// @brief Phase 10.7 slice A1 — `AudioBus` field on
///        `AudioSourceComponent` + clone-preservation coverage.

#include <gtest/gtest.h>

#include "audio/audio_mixer.h"
#include "audio/audio_source_component.h"

using namespace Vestige;

TEST(AudioSourceComponent, DefaultBusIsSfx)
{
    AudioSourceComponent source;
    EXPECT_EQ(source.bus, AudioBus::Sfx);
}

TEST(AudioSourceComponent, BusIsAssignable)
{
    AudioSourceComponent source;
    source.bus = AudioBus::Music;
    EXPECT_EQ(source.bus, AudioBus::Music);
    source.bus = AudioBus::Voice;
    EXPECT_EQ(source.bus, AudioBus::Voice);
}

TEST(AudioSourceComponent, ClonePreservesBus)
{
    AudioSourceComponent original;
    original.bus      = AudioBus::Ambient;
    original.volume   = 0.75f;
    original.clipPath = "sfx/fire_crackle.wav";

    auto cloned = original.clone();
    auto* typedClone = dynamic_cast<AudioSourceComponent*>(cloned.get());
    ASSERT_NE(typedClone, nullptr);
    EXPECT_EQ(typedClone->bus,      AudioBus::Ambient);
    EXPECT_FLOAT_EQ(typedClone->volume, 0.75f);
    EXPECT_EQ(typedClone->clipPath, "sfx/fire_crackle.wav");
}

TEST(AudioSourceComponent, CloneDoesNotShareBusState)
{
    AudioSourceComponent original;
    original.bus = AudioBus::Music;

    auto cloned = original.clone();
    auto* typedClone = dynamic_cast<AudioSourceComponent*>(cloned.get());
    ASSERT_NE(typedClone, nullptr);

    // Mutate the clone — the original must be unaffected.
    typedClone->bus = AudioBus::Voice;
    EXPECT_EQ(original.bus,   AudioBus::Music);
    EXPECT_EQ(typedClone->bus, AudioBus::Voice);
}

TEST(AudioSourceComponent, AllBusesAssignable)
{
    AudioSourceComponent source;
    for (std::size_t i = 0; i < AudioBusCount; ++i)
    {
        const AudioBus bus = static_cast<AudioBus>(i);
        source.bus = bus;
        EXPECT_EQ(source.bus, bus) << "bus index " << i;
    }
}
