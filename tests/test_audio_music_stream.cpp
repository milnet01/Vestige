// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_music_stream.cpp
/// @brief Phase 10 coverage for the streaming-music decode state
///        machine — buffer accounting, EOF / loop handling, and
///        the tick planner's chunk-sized back-pressure logic.

#include <gtest/gtest.h>

#include "audio/audio_music_stream.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-3f;
}

// -- computeStreamBufferedSeconds -------------------------------

TEST(MusicStream, BufferedSecondsIsZeroAtStart)
{
    MusicStreamState s;
    s.sampleRate = 48000;
    EXPECT_NEAR(computeStreamBufferedSeconds(s), 0.0f, kEps);
}

TEST(MusicStream, BufferedSecondsReflectsDecodedMinusConsumed)
{
    MusicStreamState s;
    s.sampleRate          = 48000;
    s.totalFramesDecoded  = 48000;  // 1 second decoded
    s.totalFramesConsumed = 24000;  // 0.5 seconds consumed
    EXPECT_NEAR(computeStreamBufferedSeconds(s), 0.5f, kEps);
}

TEST(MusicStream, BufferedSecondsZeroWhenSampleRateZero)
{
    MusicStreamState s;
    s.sampleRate          = 0;   // not yet known
    s.totalFramesDecoded  = 1000;
    EXPECT_NEAR(computeStreamBufferedSeconds(s), 0.0f, kEps);
}

TEST(MusicStream, ConsumerOverrunReturnsZeroNotNegative)
{
    // Shouldn't happen in practice but guard against it anyway.
    MusicStreamState s;
    s.sampleRate          = 48000;
    s.totalFramesDecoded  = 1000;
    s.totalFramesConsumed = 2000;
    EXPECT_NEAR(computeStreamBufferedSeconds(s), 0.0f, kEps);
}

// -- notifyStreamFramesConsumed / Decoded -------------------------

TEST(MusicStream, NotifyConsumedAdvancesCounter)
{
    MusicStreamState s;
    notifyStreamFramesConsumed(s, 10000);
    EXPECT_EQ(s.totalFramesConsumed, 10000u);
}

TEST(MusicStream, NotifyDecodedAdvancesCounterAndLoopOnEof)
{
    MusicStreamState s;
    notifyStreamFramesDecoded(s, 48000, false);
    EXPECT_EQ(s.totalFramesDecoded, 48000u);
    EXPECT_EQ(s.loopCount, 0u);
    EXPECT_FALSE(s.trackFullyDecodedOnce);

    notifyStreamFramesDecoded(s, 24000, true);
    EXPECT_EQ(s.totalFramesDecoded, 72000u);
    EXPECT_EQ(s.loopCount, 1u);
    EXPECT_TRUE(s.trackFullyDecodedOnce);
}

TEST(MusicStream, FinishedSetAfterFullDrain)
{
    MusicStreamState s;
    s.sampleRate = 48000;
    notifyStreamFramesDecoded(s, 48000, true);   // decoded + EOF
    notifyStreamFramesConsumed(s, 48000);        // consume all
    EXPECT_TRUE(s.finished);
}

TEST(MusicStream, NotFinishedUntilFullDrain)
{
    MusicStreamState s;
    notifyStreamFramesDecoded(s, 48000, true);
    notifyStreamFramesConsumed(s, 24000);
    EXPECT_FALSE(s.finished);
}

TEST(MusicStream, NotFinishedIfEofNotReachedYet)
{
    MusicStreamState s;
    notifyStreamFramesDecoded(s, 48000, false);
    notifyStreamFramesConsumed(s, 48000);
    EXPECT_FALSE(s.finished);
}

// -- planStreamTick: back-pressure --------------------------------

TEST(MusicStream, BackPressureWhenBufferedExceedsCap)
{
    MusicStreamState s;
    s.sampleRate          = 48000;
    s.minSecondsBuffered  = 0.3f;
    s.maxSecondsBuffered  = 0.6f;
    s.framesPerChunk      = 4096;
    s.totalFramesDecoded  = 48000;   // 1.0s decoded
    s.totalFramesConsumed = 0;       // nothing consumed

    auto plan = planStreamTick(s, false);
    EXPECT_EQ(plan.framesToDecode, 0u);
    EXPECT_FALSE(plan.rewindForLoop);
    EXPECT_FALSE(plan.trackFinished);
}

// -- planStreamTick: refill ---------------------------------------

TEST(MusicStream, RequestsChunkWhenBelowMinBuffered)
{
    MusicStreamState s;
    s.sampleRate          = 48000;
    s.minSecondsBuffered  = 0.3f;
    s.maxSecondsBuffered  = 0.6f;
    s.framesPerChunk      = 4096;

    auto plan = planStreamTick(s, false);
    EXPECT_EQ(plan.framesToDecode, 4096u);
    EXPECT_FALSE(plan.rewindForLoop);
}

TEST(MusicStream, RequestsChunkAfterConsumerDrain)
{
    MusicStreamState s;
    s.sampleRate          = 48000;
    s.minSecondsBuffered  = 0.3f;
    s.maxSecondsBuffered  = 0.6f;
    s.framesPerChunk      = 4096;
    s.totalFramesDecoded  = 24000;   // 0.5s
    s.totalFramesConsumed = 20000;   // 0.41s consumed → 0.08s buffered

    auto plan = planStreamTick(s, false);
    EXPECT_EQ(plan.framesToDecode, 4096u);
}

// -- planStreamTick: EOF + loop policy ----------------------------

TEST(MusicStream, RewindsForLoopWhenDecoderEofAndPolicyAllows)
{
    MusicStreamState s;
    s.sampleRate          = 48000;
    s.minSecondsBuffered  = 0.3f;
    s.maxSecondsBuffered  = 0.6f;
    s.framesPerChunk      = 4096;
    s.maxLoops            = -1;    // infinite loops

    auto plan = planStreamTick(s, true);
    EXPECT_TRUE(plan.rewindForLoop);
    EXPECT_GT(plan.framesToDecode, 0u);
    EXPECT_FALSE(plan.trackFinished);
}

TEST(MusicStream, FinishesWhenEofAndNoLoopsLeft)
{
    MusicStreamState s;
    s.sampleRate          = 48000;
    s.minSecondsBuffered  = 0.3f;
    s.framesPerChunk      = 4096;
    s.maxLoops            = 0;     // one-shot
    s.loopCount           = 1;     // already finished the single play-through

    auto plan = planStreamTick(s, true);
    EXPECT_TRUE(plan.trackFinished);
    EXPECT_TRUE(s.finished);
}

TEST(MusicStream, FinitePolicyAllowsFewerLoops)
{
    // maxLoops=2 → allow playthrough 0, 1, 2 then finish.
    MusicStreamState s;
    s.sampleRate          = 48000;
    s.minSecondsBuffered  = 0.3f;
    s.framesPerChunk      = 4096;
    s.maxLoops            = 2;

    // First pass EOF (loopCount was 0 → 1): allow another loop
    s.loopCount = 1;
    auto plan1 = planStreamTick(s, true);
    EXPECT_TRUE(plan1.rewindForLoop);

    // Second pass EOF (loopCount was 1 → 2): allow another loop
    s.loopCount = 2;
    auto plan2 = planStreamTick(s, true);
    EXPECT_TRUE(plan2.rewindForLoop);

    // Third pass EOF (loopCount was 2 → 3): exhausted → finish.
    s.loopCount = 3;
    auto plan3 = planStreamTick(s, true);
    EXPECT_TRUE(plan3.trackFinished);
}

TEST(MusicStream, FinishedStreamReportsFinishedOnFutureTicks)
{
    MusicStreamState s;
    s.finished = true;
    auto plan = planStreamTick(s, false);
    EXPECT_TRUE(plan.trackFinished);
    EXPECT_EQ(plan.framesToDecode, 0u);
}
