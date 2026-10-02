// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_source_tracker.cpp
/// @brief 3D_E-0738 — AudioSystem's auto-play start / reap cycle, driven
///        frame by frame without an audio device.
#include "audio/audio_source_tracker.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>

using namespace Vestige;

namespace
{

constexpr std::uint32_t kEntity = 7;

/// Stand-in for the AL source pool: a source plays until `stop` is called.
struct FakeSources
{
    std::set<unsigned int> playing;
    void stop(unsigned int source) { playing.erase(source); }
};

/// One AudioSystem reap pass with every entity still in the scene.
void reapFrame(AudioSourceTracker& tracker, const FakeSources& al)
{
    tracker.reap([](std::uint32_t) { return false; },
                 [&al](unsigned int s) { return al.playing.count(s) != 0; });
}

}  // namespace

TEST(AudioSourceTracker, FinishedOneShotIsNotStartedAgain)
{
    AudioSourceTracker tracker;
    FakeSources al;

    ASSERT_TRUE(tracker.shouldStart(kEntity, true, true));
    tracker.started(kEntity, 100);
    al.playing.insert(100);

    al.stop(100);  // the one-shot reaches its end
    reapFrame(tracker, al);

    EXPECT_FALSE(tracker.shouldStart(kEntity, true, true))
        << "autoPlay plays once per scene load; a finished one-shot restarted";
}

TEST(AudioSourceTracker, FailedAcquireRetriesNextFrame)
{
    AudioSourceTracker tracker;
    FakeSources al;

    tracker.started(kEntity, 0);  // pool full or file missing
    reapFrame(tracker, al);

    EXPECT_TRUE(tracker.shouldStart(kEntity, true, true));
}

TEST(AudioSourceTracker, PlayingSourceStaysTracked)
{
    AudioSourceTracker tracker;
    FakeSources al;

    tracker.started(kEntity, 100);
    al.playing.insert(100);
    reapFrame(tracker, al);

    ASSERT_NE(tracker.find(kEntity), nullptr);
    EXPECT_EQ(*tracker.find(kEntity), 100u);
    EXPECT_FALSE(tracker.shouldStart(kEntity, true, true));
}

TEST(AudioSourceTracker, NoClipOrNoAutoPlayNeverStarts)
{
    AudioSourceTracker tracker;
    EXPECT_FALSE(tracker.shouldStart(kEntity, true, false));
    EXPECT_FALSE(tracker.shouldStart(kEntity, false, true));
}

TEST(AudioSourceTracker, UntickingAutoPlayReArmsTheEntity)
{
    AudioSourceTracker tracker;
    FakeSources al;

    tracker.started(kEntity, 100);
    al.stop(100);
    reapFrame(tracker, al);
    ASSERT_FALSE(tracker.shouldStart(kEntity, true, true));

    tracker.observe(kEntity, false);  // editor unticks autoPlay ...
    tracker.observe(kEntity, true);   // ... and ticks it again
    EXPECT_TRUE(tracker.shouldStart(kEntity, true, true));
}

TEST(AudioSourceTracker, EntityLeavingTheSceneIsForgotten)
{
    AudioSourceTracker tracker;
    FakeSources al;

    tracker.started(kEntity, 100);
    al.playing.insert(100);
    tracker.reap([](std::uint32_t) { return true; },
                 [&al](unsigned int s) { return al.playing.count(s) != 0; });

    EXPECT_EQ(tracker.find(kEntity), nullptr);
    // Ids are never reused, so a fresh start would only follow a re-arm;
    // forgetting the entry is what keeps the record bounded.
    tracker.observe(kEntity, true);
    EXPECT_TRUE(tracker.shouldStart(kEntity, true, true));
}
