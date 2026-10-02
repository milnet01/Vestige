// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_bus_activity.cpp
/// @brief 3D_E-0742 — a sound played straight through AudioEngine (a
///        script's dialogue line, a stinger) marks its bus active, so the
///        Voice → Music duck route fires for it.
///
/// Opens OpenAL Soft's null backend (`ALSOFT_DRIVERS=null`) and plays a
/// short generated WAV; skips where no device opens. POSIX only, like the
/// other env-driven tests.
#include "audio/audio_engine.h"
#include "audio/audio_mixer.h"
#include "audio_device_helpers.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace Vestige;

#ifndef _WIN32

using Vestige::TestAudio::openNullAudioDevice;
using Vestige::TestAudio::writeTestWav;

TEST(AudioBusActivity, DirectPlaybackMarksItsBusActive)
{
    AudioEngine engine;
    if (!openNullAudioDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto wav = writeTestWav("vestige_bus_activity.wav", 2.0f);

    const unsigned int voice = engine.playSound2D(wav.string(), 1.0f, AudioBus::Voice,
                                                  SoundPriority::Critical);
    const unsigned int quiet = engine.playSound2D(wav.string(), 0.001f, AudioBus::Music);
    ASSERT_NE(voice, 0u);
    ASSERT_NE(quiet, 0u);

    const auto active = engine.busesWithLivePlayback(0.01f);
    EXPECT_TRUE(active[static_cast<std::size_t>(AudioBus::Voice)])
        << "a dialogue line played straight through the engine left Voice idle";
    EXPECT_FALSE(active[static_cast<std::size_t>(AudioBus::Music)])
        << "a playback under the volume floor counted as activity";
    EXPECT_FALSE(active[static_cast<std::size_t>(AudioBus::Sfx)]);

    engine.stopSound(voice);
    EXPECT_FALSE(engine.busesWithLivePlayback(0.01f)[static_cast<std::size_t>(AudioBus::Voice)])
        << "a stopped playback still counted as activity";

    engine.shutdown();
    std::filesystem::remove(wav);
}

#endif
