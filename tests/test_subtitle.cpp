// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_subtitle.cpp
/// @brief Phase 10 accessibility coverage for the subtitle / closed-
///        caption queue and size presets.

#include <gtest/gtest.h>

#include "ui/subtitle.h"

using namespace Vestige;

namespace
{
Subtitle makeLine(const std::string& text,
                  float duration = 3.0f,
                  SubtitleCategory cat = SubtitleCategory::Dialogue,
                  const std::string& speaker = "")
{
    Subtitle s;
    s.text = text;
    s.durationSeconds = duration;
    s.category = cat;
    s.speaker = speaker;
    return s;
}
}

// -- Size presets --

TEST(SubtitleSize, PresetFactorsMatchUiScaleLadder)
{
    // Intentionally the same 1.0 / 1.25 / 1.5 / 2.0 ratios as UI scale,
    // so a user who knows the UI ladder understands the subtitle ladder.
    EXPECT_FLOAT_EQ(subtitleScaleFactorOf(SubtitleSizePreset::Small),  1.00f);
    EXPECT_FLOAT_EQ(subtitleScaleFactorOf(SubtitleSizePreset::Medium), 1.25f);
    EXPECT_FLOAT_EQ(subtitleScaleFactorOf(SubtitleSizePreset::Large),  1.50f);
    EXPECT_FLOAT_EQ(subtitleScaleFactorOf(SubtitleSizePreset::XL),     2.00f);
}

// -- Empty queue baseline --

TEST(SubtitleQueue, DefaultsAreEmptyAndMedium)
{
    SubtitleQueue q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.maxConcurrent(), SubtitleQueue::DEFAULT_MAX_CONCURRENT);
    EXPECT_EQ(q.sizePreset(), SubtitleSizePreset::Medium);
}

TEST(SubtitleQueue, TickingEmptyQueueIsSafe)
{
    SubtitleQueue q;
    q.tick(1.0f);
    q.tick(100.0f);
    EXPECT_TRUE(q.empty());
}

// -- Enqueue + tick --

TEST(SubtitleQueue, EnqueueMakesLineActiveForItsDuration)
{
    SubtitleQueue q;
    q.enqueue(makeLine("The curtain billows.", 2.0f, SubtitleCategory::Narrator));

    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q.activeSubtitles()[0].remainingSeconds, 2.0f);
    EXPECT_EQ(q.activeSubtitles()[0].subtitle.text, "The curtain billows.");
    EXPECT_EQ(q.activeSubtitles()[0].subtitle.category, SubtitleCategory::Narrator);
}

TEST(SubtitleQueue, TickCountsDownRemainingTime)
{
    SubtitleQueue q;
    q.enqueue(makeLine("A", 2.0f));
    q.tick(0.5f);
    EXPECT_FLOAT_EQ(q.activeSubtitles()[0].remainingSeconds, 1.5f);
    q.tick(1.0f);
    EXPECT_FLOAT_EQ(q.activeSubtitles()[0].remainingSeconds, 0.5f);
}

TEST(SubtitleQueue, TickExpiresEntryWhenTimeReachesZero)
{
    SubtitleQueue q;
    q.enqueue(makeLine("A", 1.0f));
    q.tick(1.0f);
    EXPECT_TRUE(q.empty());
}

TEST(SubtitleQueue, TickExpiresEntryWhenTimeOvershoots)
{
    // A long frame (e.g. a hitch) should still drop the entry
    // cleanly, not leave it at negative remaining-time.
    SubtitleQueue q;
    q.enqueue(makeLine("A", 1.0f));
    q.tick(5.0f);
    EXPECT_TRUE(q.empty());
}

TEST(SubtitleQueue, OnlyExpiredEntriesAreDropped)
{
    SubtitleQueue q;
    q.enqueue(makeLine("A", 1.0f));
    q.enqueue(makeLine("B", 5.0f));
    q.tick(1.0f);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q.activeSubtitles()[0].subtitle.text, "B");
    EXPECT_FLOAT_EQ(q.activeSubtitles()[0].remainingSeconds, 4.0f);
}

TEST(SubtitleQueue, ActiveOrderIsEnqueueOrder)
{
    SubtitleQueue q;
    q.enqueue(makeLine("A"));
    q.enqueue(makeLine("B"));
    q.enqueue(makeLine("C"));
    ASSERT_EQ(q.size(), 3u);
    EXPECT_EQ(q.activeSubtitles()[0].subtitle.text, "A");
    EXPECT_EQ(q.activeSubtitles()[1].subtitle.text, "B");
    EXPECT_EQ(q.activeSubtitles()[2].subtitle.text, "C");
}

// -- Concurrent cap + eviction --

TEST(SubtitleQueue, CapOfThreeEvictsOldestOnOverflow)
{
    SubtitleQueue q;
    // Default cap is 3.
    q.enqueue(makeLine("A"));
    q.enqueue(makeLine("B"));
    q.enqueue(makeLine("C"));
    q.enqueue(makeLine("D"));  // Evicts "A".

    ASSERT_EQ(q.size(), 3u);
    EXPECT_EQ(q.activeSubtitles()[0].subtitle.text, "B");
    EXPECT_EQ(q.activeSubtitles()[1].subtitle.text, "C");
    EXPECT_EQ(q.activeSubtitles()[2].subtitle.text, "D");
}

TEST(SubtitleQueue, LoweringMaxConcurrentEvictsOldestImmediately)
{
    SubtitleQueue q;
    q.enqueue(makeLine("A"));
    q.enqueue(makeLine("B"));
    q.enqueue(makeLine("C"));

    q.setMaxConcurrent(1);

    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q.activeSubtitles()[0].subtitle.text, "C");
    EXPECT_EQ(q.maxConcurrent(), 1);
}

TEST(SubtitleQueue, RaisingMaxConcurrentDoesNotAffectActiveList)
{
    SubtitleQueue q;
    q.enqueue(makeLine("A"));
    q.enqueue(makeLine("B"));

    q.setMaxConcurrent(10);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.maxConcurrent(), 10);
}

// -- Clear --

TEST(SubtitleQueue, ClearEmptiesEverything)
{
    SubtitleQueue q;
    q.enqueue(makeLine("A"));
    q.enqueue(makeLine("B"));
    q.clear();
    EXPECT_TRUE(q.empty());
}

// -- Size preset state --

TEST(SubtitleQueue, SetSizePresetIsStored)
{
    SubtitleQueue q;
    q.setSizePreset(SubtitleSizePreset::XL);
    EXPECT_EQ(q.sizePreset(), SubtitleSizePreset::XL);
    // No effect on the active list.
    EXPECT_TRUE(q.empty());
}

// -- Category + spatial metadata round-trip --

TEST(SubtitleQueue, SoundCueWithDirectionPreservesMetadata)
{
    SubtitleQueue q;
    Subtitle cue;
    cue.text = "[rustling leaves]";
    cue.category = SubtitleCategory::SoundCue;
    cue.directionDegrees = 90.0f;  // To the right.
    cue.durationSeconds = 2.0f;
    q.enqueue(cue);

    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q.activeSubtitles()[0].subtitle.category, SubtitleCategory::SoundCue);
    EXPECT_FLOAT_EQ(q.activeSubtitles()[0].subtitle.directionDegrees, 90.0f);
}

TEST(SubtitleQueue, DialogueSpeakerPrefixPreserved)
{
    SubtitleQueue q;
    q.enqueue(makeLine("Come forth.", 3.0f, SubtitleCategory::Dialogue, "Moses"));
    EXPECT_EQ(q.activeSubtitles()[0].subtitle.speaker, "Moses");
}

// -- Robustness --

TEST(SubtitleQueue, NegativeDurationIsClampedToZero)
{
    // A caption authored with a nonsensical negative duration should
    // disappear on the first tick, not show forever.
    SubtitleQueue q;
    q.enqueue(makeLine("bogus", -5.0f));
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q.activeSubtitles()[0].remainingSeconds, 0.0f);
    q.tick(0.016f);
    EXPECT_TRUE(q.empty());
}
