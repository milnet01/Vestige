// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_source_gone.cpp
/// @brief 3D_E-0993 — the auto-played sound of an entity that has left the
///        scene is stopped, not just forgotten.
///
/// A looping sound never ends on its own. AudioSystem dropped its record of
/// a deleted entity's sound and left the sound playing.
///
/// Opens OpenAL's null device; skips where none opens. POSIX only, like the
/// device helper.

#include "audio/audio_engine.h"
#include "audio_device_helpers.h"
#include "scene/scene.h"
#include "systems/audio_system.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

namespace Vestige::AudioSourceGone::Test
{

#ifndef _WIN32
namespace
{
constexpr std::uint32_t kGoneEntity = 7;
constexpr std::uint32_t kStayingEntity = 8;

/// Starts a looping sound and records it as `entityId`'s auto-played one.
unsigned int startTrackedLoop(AudioSystem& system, const std::filesystem::path& wav,
                              std::uint32_t entityId)
{
    AudioEngine& engine = system.getAudioEngine();
    const unsigned int source = engine.playSound2D(
        wav.string(), 1.0f, AudioBus::Sfx, SoundPriority::Normal, /*loop=*/true);
    system.sourceTracker().started(entityId, source,
                                   engine.playbackTicket(source), /*loop=*/true);
    return source;
}
}  // namespace

TEST(AudioSourceGone, DeletedEntitysLoopIsStopped_0993)
{
    AudioSystem system;
    if (!TestAudio::openNullAudioDevice(system.getAudioEngine()))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto wav = TestAudio::writeTestWav("vestige_source_gone.wav", 0.5f, 1);
    const unsigned int gone = startTrackedLoop(system, wav, kGoneEntity);
    const unsigned int staying = startTrackedLoop(system, wav, kStayingEntity);
    ASSERT_NE(gone, 0u);
    ASSERT_NE(staying, 0u);
    ASSERT_TRUE(system.getAudioEngine().isSourcePlaying(gone));

    system.releaseSources([](std::uint32_t id) { return id == kGoneEntity; });

    EXPECT_FALSE(system.getAudioEngine().isSourcePlaying(gone))
        << "the deleted entity's looping sound kept playing";
    EXPECT_TRUE(system.getAudioEngine().isSourcePlaying(staying));
    EXPECT_EQ(system.activeSources().count(kGoneEntity), 0u);
    EXPECT_EQ(system.activeSources().count(kStayingEntity), 1u);

    system.getAudioEngine().stopSound(staying);
    std::filesystem::remove(wav);
}

// A source that eviction handed to another sound carries a newer ticket: it
// is no longer the deleted entity's, so it keeps playing.
TEST(AudioSourceGone, SourceNowOwnedByAnotherSoundKeepsPlaying_0993)
{
    AudioSystem system;
    if (!TestAudio::openNullAudioDevice(system.getAudioEngine()))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    AudioEngine& engine = system.getAudioEngine();
    const auto wav = TestAudio::writeTestWav("vestige_source_gone_evicted.wav", 0.5f, 1);
    const unsigned int source = engine.playSound2D(
        wav.string(), 1.0f, AudioBus::Sfx, SoundPriority::Normal, /*loop=*/true);
    ASSERT_NE(source, 0u);
    const std::uint64_t staleTicket = engine.playbackTicket(source) + 1000;
    system.sourceTracker().started(kGoneEntity, source, staleTicket, /*loop=*/true);

    system.releaseSources([](std::uint32_t) { return true; });

    EXPECT_TRUE(engine.isSourcePlaying(source));
    EXPECT_TRUE(system.activeSources().empty());

    engine.stopSound(source);
    std::filesystem::remove(wav);
}

// 3D_E-0874 — closing a scene stops the sounds its entities auto-played.
TEST(AudioSourceGone, SceneUnloadStopsAutoPlayedLoops_0874)
{
    AudioSystem system;
    if (!TestAudio::openNullAudioDevice(system.getAudioEngine()))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto wav = TestAudio::writeTestWav("vestige_source_gone_unload.wav", 0.5f, 1);
    const unsigned int source = startTrackedLoop(system, wav, kGoneEntity);
    ASSERT_NE(source, 0u);
    ASSERT_TRUE(system.getAudioEngine().isSourcePlaying(source));

    Scene scene("unloading");
    system.onSceneUnload(scene);

    EXPECT_FALSE(system.getAudioEngine().isSourcePlaying(source));
    EXPECT_TRUE(system.activeSources().empty());
    std::filesystem::remove(wav);
}
#endif

}  // namespace Vestige::AudioSourceGone::Test
