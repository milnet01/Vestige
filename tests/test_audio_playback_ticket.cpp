// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_playback_ticket.cpp
/// @brief 3D_E-0739 — AudioEngine gives every grant of a pool source a
///        ticket that never repeats, so a holder can tell its playback
///        from the next sound that takes the same source name.
///
/// Opens a real OpenAL device. `ALSOFT_DRIVERS=null` selects OpenAL Soft's
/// silent null backend, which the bundled build includes; where no device
/// opens the test skips. POSIX only, like test_settings.cpp's env tests.
#include "audio/audio_engine.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

using namespace Vestige;

#ifndef _WIN32

TEST(AudioEnginePlaybackTicket, ReacquiredSourceGetsANewTicket)
{
    const char* previous = std::getenv("ALSOFT_DRIVERS");
    const std::string saved = (previous != nullptr) ? previous : "";
    ::setenv("ALSOFT_DRIVERS", "null", 1);

    AudioEngine engine;
    const bool opened = engine.initialize();
    if (previous != nullptr) ::setenv("ALSOFT_DRIVERS", saved.c_str(), 1);
    else          ::unsetenv("ALSOFT_DRIVERS");
    if (!opened)
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

#endif
