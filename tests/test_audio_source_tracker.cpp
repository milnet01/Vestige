// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_source_tracker.cpp
/// @brief 3D_E-0738 / 3D_E-0739 — AudioSystem's auto-play start / reap
///        cycle, driven frame by frame without an audio device.
#include "audio/audio_source_tracker.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_map>

using namespace Vestige;

namespace
{

constexpr std::uint32_t kEntity = 7;

/// Stand-in for the AL source pool: each playing source carries the ticket
/// of the playback holding it, as `AudioEngine::playbackTicket` reports.
struct FakeSources
{
    std::unordered_map<unsigned int, std::uint64_t> playing;
    std::uint64_t nextTicket = 1;

    std::uint64_t play(unsigned int source)
    {
        playing[source] = nextTicket;
        return nextTicket++;
    }
    void stop(unsigned int source) { playing.erase(source); }

    bool owns(const AudioSourceTracker::TrackedSource& t) const
    {
        auto it = playing.find(t.source);
        return it != playing.end() && it->second == t.ticket;
    }
};

/// One AudioSystem reap pass with every entity still in the scene.
void reapFrame(AudioSourceTracker& tracker, const FakeSources& al)
{
    tracker.reap([](std::uint32_t) { return false; },
                 [&al](const AudioSourceTracker::TrackedSource& t) { return al.owns(t); });
}

}  // namespace

TEST(AudioSourceTracker, FinishedOneShotIsNotStartedAgain)
{
    AudioSourceTracker tracker;
    FakeSources al;

    ASSERT_TRUE(tracker.shouldStart(kEntity, true, true));
    tracker.started(kEntity, 100, al.play(100), false);

    al.stop(100);  // the one-shot reaches its end
    reapFrame(tracker, al);

    EXPECT_FALSE(tracker.shouldStart(kEntity, true, true))
        << "autoPlay plays once per scene load; a finished one-shot restarted";
}

TEST(AudioSourceTracker, FailedAcquireRetriesNextFrame)
{
    AudioSourceTracker tracker;
    FakeSources al;

    tracker.started(kEntity, 0, 0, false);  // pool full or file missing
    reapFrame(tracker, al);

    EXPECT_TRUE(tracker.shouldStart(kEntity, true, true));
}

TEST(AudioSourceTracker, PlayingSourceStaysTracked)
{
    AudioSourceTracker tracker;
    FakeSources al;

    tracker.started(kEntity, 100, al.play(100), false);
    reapFrame(tracker, al);

    ASSERT_NE(tracker.find(kEntity), nullptr);
    EXPECT_EQ(tracker.find(kEntity)->source, 100u);
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

    tracker.started(kEntity, 100, al.play(100), false);
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

    tracker.started(kEntity, 100, al.play(100), false);
    tracker.reap([](std::uint32_t) { return true; },
                 [&al](const AudioSourceTracker::TrackedSource& t) { return al.owns(t); });

    EXPECT_EQ(tracker.find(kEntity), nullptr);
    // Ids are never reused, so a fresh start would only follow a re-arm;
    // forgetting the entry is what keeps the record bounded.
    tracker.observe(kEntity, true);
    EXPECT_TRUE(tracker.shouldStart(kEntity, true, true));
}

TEST(AudioSourceTracker, SourceTakenByAnotherSoundIsDropped)
{
    AudioSourceTracker tracker;
    FakeSources al;

    tracker.started(kEntity, 100, al.play(100), false);
    al.play(100);  // evicted: the same source name now plays another sound
    reapFrame(tracker, al);

    EXPECT_EQ(tracker.find(kEntity), nullptr)
        << "the entity still claims a source that belongs to another sound";
    EXPECT_FALSE(tracker.shouldStart(kEntity, true, true))
        << "an evicted one-shot is not restarted";
}

TEST(AudioSourceTracker, EvictedLoopRestartsWhenASourceFrees)
{
    AudioSourceTracker tracker;
    FakeSources al;

    tracker.started(kEntity, 100, al.play(100), true);
    al.play(100);  // evicted
    reapFrame(tracker, al);

    EXPECT_EQ(tracker.find(kEntity), nullptr);
    EXPECT_TRUE(tracker.shouldStart(kEntity, true, true))
        << "a loop never ends on its own, so losing it re-arms the entity";
}
