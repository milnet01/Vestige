// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_channel_formats.cpp
/// @brief 3D_E-0743 — every channel count a clip can carry maps to the
///        OpenAL format that plays it, 4-channel data is marked AmbiX on the
///        buffer, and a count nothing can play is refused at load.
#include "audio/audio_clip.h"
#include "audio/audio_engine.h"
#include "audio/audio_source_state.h"
#include "audio_device_helpers.h"

#include <gtest/gtest.h>

#include <AL/al.h>
#include <AL/alext.h>

#include <filesystem>

using namespace Vestige;

TEST(AudioChannelFormat, MapsSupportedLayoutsAndRefusesTheRest)
{
    EXPECT_EQ(alFormatForChannels(1), AL_FORMAT_MONO16);
    EXPECT_EQ(alFormatForChannels(2), AL_FORMAT_STEREO16);
    EXPECT_EQ(alFormatForChannels(4), AL_FORMAT_BFORMAT3D_16);
    EXPECT_EQ(alFormatForChannels(6), AL_FORMAT_51CHN16);
    EXPECT_EQ(alFormatForChannels(8), AL_FORMAT_71CHN16);
    for (std::uint32_t refused : {0u, 3u, 5u, 7u, 9u})
    {
        EXPECT_EQ(alFormatForChannels(refused), 0) << refused << " channels";
    }
}

TEST(AudioChannelFormat, ClipLoadRefusesAnUnplayableLayout)
{
    const auto three = TestAudio::writeTestWav("vestige_3ch.wav", 0.1f, 3);
    EXPECT_FALSE(AudioClip::loadFromFile(three.string()).has_value())
        << "a 3-channel clip loaded, and would upload in the wrong format";
    std::filesystem::remove(three);

    const auto four = TestAudio::writeTestWav("vestige_4ch.wav", 0.1f, 4);
    const auto clip = AudioClip::loadFromFile(four.string());
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->getChannels(), 4u);
    EXPECT_EQ(clip->getALFormat(), AL_FORMAT_BFORMAT3D_16);
    std::filesystem::remove(four);
}

#ifndef _WIN32

TEST(AudioChannelFormat, BuffersCarryTheirChannelLayout)
{
    AudioEngine engine;
    if (!TestAudio::openNullAudioDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }

    const auto four = TestAudio::writeTestWav("vestige_ambix.wav", 0.1f, 4);
    const ALuint ambi = engine.loadBuffer(four.string());
    ASSERT_NE(ambi, 0u);
    ALint value = 0;
    alGetBufferi(ambi, AL_CHANNELS, &value);
    EXPECT_EQ(value, 4) << "4-channel data uploaded in another format";
    alGetBufferi(ambi, AL_AMBISONIC_LAYOUT_SOFT, &value);
    EXPECT_EQ(value, AL_ACN_SOFT) << "AmbiX data left in OpenAL's FuMa channel order";
    alGetBufferi(ambi, AL_AMBISONIC_SCALING_SOFT, &value);
    EXPECT_EQ(value, AL_SN3D_SOFT) << "AmbiX data left in OpenAL's FuMa scaling";

    const auto six = TestAudio::writeTestWav("vestige_51.wav", 0.1f, 6);
    const ALuint surround = engine.loadBuffer(six.string());
    ASSERT_NE(surround, 0u);
    alGetBufferi(surround, AL_CHANNELS, &value);
    EXPECT_EQ(value, 6) << "5.1 data uploaded in another format";

    engine.shutdown();
    std::filesystem::remove(four);
    std::filesystem::remove(six);
}

TEST(AudioChannelFormat, AmbisonicPlaybackIsWorldLocked)  // 3D_E-S0099
{
    AudioEngine engine;
    if (!TestAudio::openNullAudioDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto four = TestAudio::writeTestWav("vestige_ambix_play.wav", 1.0f, 4);
    const auto mono = TestAudio::writeTestWav("vestige_mono_play.wav", 1.0f, 1);

    const unsigned int field = engine.playSound2D(four.string(), 1.0f, AudioBus::Ambient);
    const unsigned int voice = engine.playSound2D(mono.string(), 1.0f, AudioBus::Voice);
    ASSERT_NE(field, 0u);
    ASSERT_NE(voice, 0u);

    ALint relative = -1;
    ALfloat rolloff = -1.0f;
    alGetSourcei(field, AL_SOURCE_RELATIVE, &relative);
    alGetSourcef(field, AL_ROLLOFF_FACTOR, &rolloff);
    EXPECT_EQ(relative, AL_FALSE) << "the soundfield is pinned to the listener's head";
    EXPECT_FLOAT_EQ(rolloff, 0.0f) << "the soundfield fades with distance from the origin";
    alGetSourcei(voice, AL_SOURCE_RELATIVE, &relative);
    EXPECT_EQ(relative, AL_TRUE) << "a mono 2D sound left the listener";

    // The per-frame component push keeps the soundfield world-locked too.
    AudioSourceAlState state;
    state.spatial = false;
    state.rolloffFactor = 1.0f;
    engine.applySourceState(field, state);
    alGetSourcei(field, AL_SOURCE_RELATIVE, &relative);
    alGetSourcef(field, AL_ROLLOFF_FACTOR, &rolloff);
    EXPECT_EQ(relative, AL_FALSE);
    EXPECT_FLOAT_EQ(rolloff, 0.0f);

    engine.shutdown();
    std::filesystem::remove(four);
    std::filesystem::remove(mono);
}

#endif
