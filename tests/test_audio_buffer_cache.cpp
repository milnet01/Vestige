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
/// enforcement, flush idempotency. The end-to-end loadBuffer→evict
/// path is exercised by integration tests on machines with audio.

#include <gtest/gtest.h>
#include "audio/audio_engine.h"

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

TEST_F(AudioBufferCacheTest, SetLimitToZeroOnEmptyCacheIsSafe)
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

}  // namespace Vestige::AudioBufferCache::Test
