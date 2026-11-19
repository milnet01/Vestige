// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_source_reuse.cpp
/// @brief 3D_E-0874 — a pooled source handed to a new sound starts clean.
///
/// applySourceState sets pitch, a low-pass filter and a reverb send on a
/// component-driven source. When that source went back to the pool and a
/// plain playSound took it, the new sound kept the old pitch.
///
/// Opens OpenAL's null device; skips where none opens. POSIX only, like the
/// device helper.

#include "audio/audio_engine.h"
#include "audio/audio_source_state.h"
#include "audio_device_helpers.h"

#include <gtest/gtest.h>
#include <AL/al.h>

#include <filesystem>

namespace Vestige::AudioSourceReuse::Test
{

#ifndef _WIN32
TEST(AudioSourceReuse, ReusedSourceStartsAtNormalPitch_0874)
{
    AudioEngine engine;
    if (!TestAudio::openNullAudioDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto wav = TestAudio::writeTestWav("vestige_source_reuse.wav", 0.5f, 1);

    const unsigned int first = engine.playSound2D(wav.string(), 1.0f, AudioBus::Sfx,
                                                  SoundPriority::Normal, /*loop=*/true);
    ASSERT_NE(first, 0u);
    AudioSourceAlState state;
    state.pitch = 2.0f;
    state.lowPassGainHf = 0.25f;
    state.reverbSend = 1.0f;
    engine.applySourceState(first, state);
    ALfloat pitch = 0.0f;
    alGetSourcef(first, AL_PITCH, &pitch);
    ASSERT_FLOAT_EQ(pitch, 2.0f);
    engine.stopSound(first);

    const unsigned int second = engine.playSound2D(wav.string(), 1.0f, AudioBus::Sfx,
                                                   SoundPriority::Normal, /*loop=*/true);
    ASSERT_EQ(second, first) << "the pool handed out a different source; "
                                "the test no longer exercises reuse";
    alGetSourcef(second, AL_PITCH, &pitch);
    EXPECT_FLOAT_EQ(pitch, 1.0f) << "the new sound kept the old sound's pitch";

    engine.stopSound(second);
    std::filesystem::remove(wav);
}
#endif

}  // namespace Vestige::AudioSourceReuse::Test
