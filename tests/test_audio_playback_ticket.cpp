// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_playback_ticket.cpp
/// @brief AudioEngine's source pool against a real device: every grant gets
///        a ticket that never repeats (3D_E-0739), and a source a caller
///        holds is never reclaimed from under it (3D_E-0740).
///
/// Opens a real OpenAL device. `ALSOFT_DRIVERS=null` selects OpenAL Soft's
/// silent null backend, which the bundled build includes; where no device
/// opens the test skips. POSIX only, like test_settings.cpp's env tests.
#include "audio/audio_engine.h"

#include <gtest/gtest.h>

#include <AL/al.h>

#include <cstdlib>
#include <string>

using namespace Vestige;

#ifndef _WIN32

namespace
{

/// Opens `engine` on OpenAL Soft's null backend, restoring ALSOFT_DRIVERS.
bool openNullDevice(AudioEngine& engine)
{
    const char* previous = std::getenv("ALSOFT_DRIVERS");
    const std::string saved = (previous != nullptr) ? previous : "";
    ::setenv("ALSOFT_DRIVERS", "null", 1);
    const bool opened = engine.initialize();
    if (previous != nullptr) ::setenv("ALSOFT_DRIVERS", saved.c_str(), 1);
    else                     ::unsetenv("ALSOFT_DRIVERS");
    return opened;
}

}  // namespace

TEST(AudioEnginePlaybackTicket, ReacquiredSourceGetsANewTicket)
{
    AudioEngine engine;
    if (!openNullDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }

    const unsigned int source = engine.acquireSource(SoundPriority::Normal);
    ASSERT_NE(source, 0u);
    const std::uint64_t first = engine.playbackTicket(source);
    EXPECT_NE(first, 0u);

    engine.releaseSource(source);
    EXPECT_EQ(engine.playbackTicket(source), 0u) << "a free source has no ticket";

    // The first free slot is the one just released, so the same name comes
    // back — as it does when eviction hands a victim's source on.
    const unsigned int again = engine.acquireSource(SoundPriority::Normal);
    ASSERT_EQ(again, source);
    EXPECT_NE(engine.playbackTicket(again), first)
        << "a second grant of the same source reused the first grant's ticket";

    engine.shutdown();
}

TEST(AudioEnginePlaybackTicket, HeldSourceIsNotReclaimedWhenItStops)
{
    AudioEngine engine;
    if (!openNullDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }

    // A music layer holds its source this way and reads AL_STOPPED after an
    // underrun until it queues more audio.
    const unsigned int held = engine.acquireSource(SoundPriority::Normal);
    ASSERT_NE(held, 0u);
    const std::uint64_t ticket = engine.playbackTicket(held);
    alSourcePlay(held);  // AL_INITIAL only becomes AL_STOPPED after a play
    alSourceStop(held);
    ALint state = 0;
    alGetSourcei(held, AL_SOURCE_STATE, &state);
    ASSERT_EQ(state, AL_STOPPED);

    engine.updateGains();  // runs the reclaim pass

    EXPECT_EQ(engine.playbackTicket(held), ticket)
        << "the engine reclaimed a source its caller still holds";
    const unsigned int next = engine.acquireSource(SoundPriority::Normal);
    EXPECT_NE(next, held) << "a held source was handed to another caller";

    engine.shutdown();
}

#endif
