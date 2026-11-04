// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_source_reuse.cpp
/// @brief 3D_E-0869 — every way of starting a sound resets the state a
///        pooled OpenAL source kept from its previous sound.
///
/// A moving sound sets the source's velocity (Doppler). When that source is
/// reused, the next sound must start still, or it plays pitch-shifted.
/// Opens OpenAL Soft's null backend; skips where no device opens. POSIX only,
/// like the other env-driven tests.
#include "audio/audio_attenuation.h"
#include "audio/audio_engine.h"
#include "audio_device_helpers.h"

#include <gtest/gtest.h>

#include <AL/al.h>

#include <filesystem>
#include <glm/glm.hpp>

using namespace Vestige;

#ifndef _WIN32

using Vestige::TestAudio::openNullAudioDevice;
using Vestige::TestAudio::writeTestWav;

namespace
{

glm::vec3 sourceVelocity(unsigned int source)
{
    glm::vec3 v(-1.0f);
    alGetSource3f(source, AL_VELOCITY, &v.x, &v.y, &v.z);
    return v;
}

/// Plays a moving sound with wide attenuation, stops it, and returns the
/// freed source so the next play reuses it.
unsigned int leaveMovingSourceBehind(AudioEngine& engine, const std::string& wav)
{
    AttenuationParams wide;
    wide.referenceDistance = 7.0f;
    wide.maxDistance       = 300.0f;
    wide.rolloffFactor     = 0.25f;
    const unsigned int moving = engine.playSoundSpatial(
        wav, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(25.0f, 0.0f, 0.0f), wide);
    EXPECT_NE(moving, 0u);
    EXPECT_EQ(sourceVelocity(moving), glm::vec3(25.0f, 0.0f, 0.0f));
    engine.stopSound(moving);
    return moving;
}

} // namespace

TEST(AudioSourceReuse, PlaySoundStartsStillOnAReusedSource)
{
    AudioEngine engine;
    if (!openNullAudioDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto wav = writeTestWav("vestige_source_reuse_a.wav", 1.0f);

    const unsigned int freed = leaveMovingSourceBehind(engine, wav.string());
    const unsigned int next  = engine.playSound(wav.string(), glm::vec3(0.0f));
    ASSERT_EQ(next, freed) << "the test needs the freed source to be reused";

    EXPECT_EQ(sourceVelocity(next), glm::vec3(0.0f))
        << "playSound kept the previous sound's velocity (Doppler shift)";

    // playSound's fixed attenuation replaces the previous sound's.
    float ref = 0.0f;
    float maxDist = 0.0f;
    float rolloff = 0.0f;
    alGetSourcef(next, AL_REFERENCE_DISTANCE, &ref);
    alGetSourcef(next, AL_MAX_DISTANCE, &maxDist);
    alGetSourcef(next, AL_ROLLOFF_FACTOR, &rolloff);
    EXPECT_FLOAT_EQ(ref, 1.0f);
    EXPECT_FLOAT_EQ(maxDist, 50.0f);
    EXPECT_FLOAT_EQ(rolloff, 1.0f);

    engine.shutdown();
    std::filesystem::remove(wav);
}

TEST(AudioSourceReuse, PlaySound2DStartsStillOnAReusedSource)
{
    AudioEngine engine;
    if (!openNullAudioDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto wav = writeTestWav("vestige_source_reuse_b.wav", 1.0f);

    const unsigned int freed = leaveMovingSourceBehind(engine, wav.string());
    const unsigned int next  = engine.playSound2D(wav.string());
    ASSERT_EQ(next, freed) << "the test needs the freed source to be reused";

    EXPECT_EQ(sourceVelocity(next), glm::vec3(0.0f))
        << "playSound2D kept the previous sound's velocity (Doppler shift)";

    ALint relative = AL_FALSE;
    alGetSourcei(next, AL_SOURCE_RELATIVE, &relative);
    EXPECT_EQ(relative, AL_TRUE) << "a mono 2D sound is pinned to the listener";

    engine.shutdown();
    std::filesystem::remove(wav);
}

TEST(AudioSourceReuse, StillSpatialPlayResetsVelocityOnAReusedSource)
{
    AudioEngine engine;
    if (!openNullAudioDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto wav = writeTestWav("vestige_source_reuse_c.wav", 1.0f);

    const unsigned int freed = leaveMovingSourceBehind(engine, wav.string());
    AttenuationParams params;
    const unsigned int next = engine.playSoundSpatial(wav.string(), glm::vec3(1.0f), params);
    ASSERT_EQ(next, freed) << "the test needs the freed source to be reused";

    EXPECT_EQ(sourceVelocity(next), glm::vec3(0.0f));
    float ref = 0.0f;
    alGetSourcef(next, AL_REFERENCE_DISTANCE, &ref);
    EXPECT_FLOAT_EQ(ref, params.referenceDistance);

    engine.shutdown();
    std::filesystem::remove(wav);
}

#endif
