// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_no_scene_ducking.cpp
/// @brief 3D_E-0874 lane-20 audio_system 129 — with no scene loaded, a
///        dialogue line played straight through the audio engine still
///        ducks the music.
///
/// AudioSystem::update returned before refreshing which buses were playing
/// when there was no active scene, so the Voice → Music route never fired
/// until a scene existed. The first Engine-in-a-test: a bare Engine (no
/// window, no initialize) is enough for AudioSystem, on OpenAL Soft's null
/// backend. Skips where no device opens. POSIX only, like the other
/// env-driven audio tests.
#include "audio/audio_ducking.h"
#include "audio/audio_engine.h"
#include "core/engine.h"
#include "systems/audio_system.h"
#include "audio_device_helpers.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace Vestige;

#ifndef _WIN32

using Vestige::TestAudio::writeTestWav;

TEST(AudioNoSceneDucking, DialogueDucksMusicBeforeAnySceneExists)
{
    Engine engine;  // bare: no window, no scene
    ASSERT_EQ(engine.getSceneManager().getActiveScene(), nullptr);

    AudioSystem audio;
    ::setenv("ALSOFT_DRIVERS", "null", 1);
    audio.initialize(engine);
    ::unsetenv("ALSOFT_DRIVERS");
    if (!audio.isAvailable())
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }

    DuckingRoute route;
    route.source = AudioBus::Voice;
    route.target = AudioBus::Music;
    route.params.attackSeconds = 0.05f;
    route.params.duckFactor    = 0.25f;
    audio.setDuckingRoutes({route});

    const auto wav = writeTestWav("vestige_no_scene_duck.wav", 2.0f);
    const unsigned int voice = audio.getAudioEngine().playSound2D(
        wav.string(), 1.0f, AudioBus::Voice, SoundPriority::Critical);
    ASSERT_NE(voice, 0u);

    for (int frame = 0; frame < 30; ++frame)  // half a second at 60 FPS
    {
        audio.update(1.0f / 60.0f);
    }

    const float musicDuck = audio.getAudioEngine().getBusDuckSnapshot()
        [static_cast<std::size_t>(AudioBus::Music)];
    EXPECT_LT(musicDuck, 0.5f)
        << "dialogue playing with no scene loaded left the music at full volume";

    audio.getAudioEngine().stopSound(voice);
    audio.shutdown();
    std::filesystem::remove(wav);
}

#endif
