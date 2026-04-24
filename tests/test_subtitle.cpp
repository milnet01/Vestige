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

// =============================================================================
// Phase 10.9 Slice 2 P1 — wrapSubtitleText: 40-char soft-wrap + 2-line cap
// =============================================================================
//
// PHASE10_7_DESIGN.md §4.2: "Line budget: soft-wrap at 40 characters; hard
// max 2 lines per entry." Captions that exceed this quietly overflowed the
// plate in shipping code; these tests pin the word-boundary packing and
// the ellipsis-truncate behaviour.

TEST(SubtitleWrap, EmptyInputReturnsEmpty_P1)
{
    auto lines = wrapSubtitleText("");
    EXPECT_TRUE(lines.empty());
}

TEST(SubtitleWrap, ShortTextReturnsSingleLine_P1)
{
    // 22 chars, well under the 40-char soft wrap.
    auto lines = wrapSubtitleText("Draw near the mountain.");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "Draw near the mountain.");
}

TEST(SubtitleWrap, ExactBoundaryFitsOnOneLine_P1)
{
    // Build an input that is exactly 40 chars (including spaces).
    const std::string exact40 = "Moses climbed Sinai under thunder clouds"; // 40 chars
    ASSERT_EQ(exact40.size(), 40u);
    auto lines = wrapSubtitleText(exact40);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], exact40);
}

TEST(SubtitleWrap, WrapsOnWordBoundary_P1)
{
    // Greedy word packing: first line fills up under 40 chars, next
    // word would push past, so new line starts. Result: NO split
    // mid-word.
    // 45 chars total, should split after "clouds".
    const std::string text =
        "Moses climbed Sinai under thunder clouds now";
    auto lines = wrapSubtitleText(text);
    ASSERT_EQ(lines.size(), 2u);
    // Line 1 is the 40-char block above ("Moses...clouds").
    EXPECT_EQ(lines[0], "Moses climbed Sinai under thunder clouds");
    EXPECT_EQ(lines[1], "now");
}

TEST(SubtitleWrap, HardBreaksOverlongToken_P1)
{
    // A single token > 40 chars must hard-break at the limit, not
    // overflow the plate. Token is 55 chars.
    const std::string longWord(55, 'X');
    auto lines = wrapSubtitleText(longWord);
    ASSERT_GE(lines.size(), 2u);
    EXPECT_LE(lines[0].size(), 40u);
    // First line is exactly 40 X's; remainder spills.
    EXPECT_EQ(lines[0], std::string(40, 'X'));
}

TEST(SubtitleWrap, ClampsToMaxLinesWithEllipsis_P1)
{
    // A 3-line input must truncate to 2 lines and end in "…".
    // Build three ~35-char chunks joined by spaces so each fills a
    // line cleanly.
    const std::string line1(35, 'a');
    const std::string line2(35, 'b');
    const std::string line3(35, 'c');
    const std::string text = line1 + " " + line2 + " " + line3;
    auto lines = wrapSubtitleText(text);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], line1);
    // Second line must carry the ellipsis marker. Greedy packing +
    // truncation is implementation-defined beyond that, but the user
    // MUST see the "…" so they know content was cut.
    const std::string ellipsis = "\xE2\x80\xA6"; // U+2026
    EXPECT_NE(lines[1].find(ellipsis), std::string::npos)
        << "Truncated caption must suffix with U+2026 so the user "
           "knows content was trimmed, not silently dropped.";
}

TEST(SubtitleWrap, CustomMaxLinesOneEllipsizesOverflow_P1)
{
    // maxLines=1 reduces even a two-line natural wrap to one line.
    const std::string text =
        "One two three four five six seven eight nine ten eleven";
    auto lines = wrapSubtitleText(text, SUBTITLE_SOFT_WRAP_CHARS, 1u);
    ASSERT_EQ(lines.size(), 1u);
    const std::string ellipsis = "\xE2\x80\xA6";
    EXPECT_NE(lines[0].find(ellipsis), std::string::npos);
}

TEST(SubtitleWrap, SingleWordUnderLimitIsPreserved_P1)
{
    auto lines = wrapSubtitleText("Hallelujah");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "Hallelujah");
}
