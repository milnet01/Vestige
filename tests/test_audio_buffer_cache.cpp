// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_buffer_cache.cpp
/// @brief Phase 10.9 Slice 8 W8 — pin AudioEngine buffer-cache LRU + flush.
///
/// Tests run without an OpenAL device — the public surface
/// (`setBufferCacheLimit`, `getBufferCacheLimit`, `getBufferCacheSize`,
/// `flushBufferCache`) doesn't depend on `m_available`, and the
/// eviction loop's `alDeleteBuffers` call is gated behind
/// `m_available && buffer != 0` so a no-device environment is safe.
///
/// `loadBuffer` short-circuits to 0 without a device, so we can't
/// directly populate the cache from disk in this test. Coverage here
/// focuses on the API contract: defaults, setter retroactive
/// enforcement, flush idempotency. The eviction test at the end opens
/// OpenAL's null device to drive loadBuffer→evict end to end.

#include <gtest/gtest.h>
#include "audio/audio_engine.h"
#include "audio_device_helpers.h"

#include <AL/al.h>

#include <filesystem>

namespace Vestige::AudioBufferCache::Test
{

class AudioBufferCacheTest : public ::testing::Test
{
protected:
    AudioEngine m_engine;
    // Deliberately do NOT call initialize() — most CI runners have no
    // audio device, and the cache surface we're testing doesn't need one.
};

TEST_F(AudioBufferCacheTest, DefaultLimitMatchesConstant)
{
    EXPECT_EQ(m_engine.getBufferCacheLimit(),
              AudioEngine::kDefaultBufferCacheLimit);
}

TEST_F(AudioBufferCacheTest, SetLimitRoundTrips)
{
    m_engine.setBufferCacheLimit(42);
    EXPECT_EQ(m_engine.getBufferCacheLimit(), 42u);
}

TEST_F(AudioBufferCacheTest, FreshEngineHasEmptyCache)
{
    EXPECT_EQ(m_engine.getBufferCacheSize(), 0u);
}

TEST_F(AudioBufferCacheTest, FlushOnEmptyCacheIsNoOp)
{
    m_engine.flushBufferCache();
    EXPECT_EQ(m_engine.getBufferCacheSize(), 0u);
    // Idempotent.
    m_engine.flushBufferCache();
    EXPECT_EQ(m_engine.getBufferCacheSize(), 0u);
}

// Honest scope: the setter's retroactive-eviction behaviour requires a
// populated cache, which `loadBuffer` can't produce here without an
// OpenAL device (see file header). This test pins the *getter
// round-trip* on an empty cache; the eviction half of the contract is
// exercised by integration tests on machines with audio.
TEST_F(AudioBufferCacheTest, SetLimitToZeroOnEmptyCacheRoundTrips)
{
    m_engine.setBufferCacheLimit(0);
    EXPECT_EQ(m_engine.getBufferCacheLimit(), 0u);
    EXPECT_EQ(m_engine.getBufferCacheSize(), 0u);
}

// W8 also adds `kDefaultBufferCacheLimit` as a public constant —
// pinning the value ensures projects that expect "256" don't get
// silently changed.
TEST_F(AudioBufferCacheTest, DefaultLimitIs256)
{
    EXPECT_EQ(AudioEngine::kDefaultBufferCacheLimit, 256u);
}

#ifndef _WIN32

// 3D_E-0874 lane 10 — eviction must skip a buffer a playing source holds.
// OpenAL refuses to delete it, so deleting leaked it and left an
// AL_INVALID_OPERATION that the next upload read as its own failure.
TEST(AudioBufferCacheEviction, AttachedBufferIsKeptAndLaterLoadsSucceed)
{
    AudioEngine engine;
    if (!TestAudio::openNullAudioDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    engine.setBufferCacheLimit(1);

    const auto a = TestAudio::writeTestWav("vestige_evict_a.wav", 0.5f, 1);
    const auto b = TestAudio::writeTestWav("vestige_evict_b.wav", 0.5f, 1);
    const auto c = TestAudio::writeTestWav("vestige_evict_c.wav", 0.5f, 1);

    const ALuint bufferA = engine.loadBuffer(a.string());
    ASSERT_NE(bufferA, 0u);
    const unsigned int voice = engine.playSound2D(a.string(), 1.0f, AudioBus::Ui,
                                                  SoundPriority::Normal, /*loop=*/true);
    ASSERT_NE(voice, 0u);

    EXPECT_NE(engine.loadBuffer(b.string()), 0u);
    EXPECT_NE(engine.loadBuffer(c.string()), 0u)
        << "a load after evicting a playing buffer failed";
    EXPECT_EQ(alIsBuffer(bufferA), AL_TRUE);
    EXPECT_EQ(engine.loadBuffer(a.string()), bufferA)
        << "the playing buffer left the cache, so its handle leaked";

    engine.stopSound(voice);
    std::filesystem::remove(a);
    std::filesystem::remove(b);
    std::filesystem::remove(c);
}

#endif

}  // namespace Vestige::AudioBufferCache::Test
