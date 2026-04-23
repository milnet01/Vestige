// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_caption_map.cpp
/// @brief Phase 10.7 slice B3 — declarative caption map coverage.

#include <gtest/gtest.h>

#include "ui/caption_map.h"
#include "ui/subtitle.h"

using namespace Vestige;

TEST(CaptionMap, EmptyByDefault)
{
    CaptionMap map;
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0u);
    EXPECT_EQ(map.lookup("anything"), nullptr);
}

TEST(CaptionMap, ParsesSingleDialogueEntry)
{
    CaptionMap map;
    const std::string json = R"({
        "audio/dialogue/moses_01.wav": {
            "category": "Dialogue",
            "speaker":  "Moses",
            "text":     "Draw near the mountain.",
            "duration": 3.5
        }
    })";
    ASSERT_TRUE(map.loadFromString(json));
    ASSERT_EQ(map.size(), 1u);

    const Subtitle* entry = map.lookup("audio/dialogue/moses_01.wav");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->speaker,        "Moses");
    EXPECT_EQ(entry->text,           "Draw near the mountain.");
    EXPECT_EQ(entry->category,       SubtitleCategory::Dialogue);
    EXPECT_FLOAT_EQ(entry->durationSeconds, 3.5f);
}

TEST(CaptionMap, ParsesNarratorAndSoundCueCategories)
{
    CaptionMap map;
    const std::string json = R"({
        "narrator.wav": { "category": "Narrator", "text": "n" },
        "cue.wav":      { "category": "SoundCue", "text": "wind howls" }
    })";
    ASSERT_TRUE(map.loadFromString(json));
    ASSERT_EQ(map.size(), 2u);

    EXPECT_EQ(map.lookup("narrator.wav")->category,
              SubtitleCategory::Narrator);
    EXPECT_EQ(map.lookup("cue.wav")->category,
              SubtitleCategory::SoundCue);
}

TEST(CaptionMap, UnknownCategoryDefaultsToDialogue)
{
    CaptionMap map;
    const std::string json = R"({
        "weird.wav": { "category": "Gibberish", "text": "t" }
    })";
    ASSERT_TRUE(map.loadFromString(json));
    EXPECT_EQ(map.lookup("weird.wav")->category,
              SubtitleCategory::Dialogue);
}

TEST(CaptionMap, MissingDurationFallsBackToDefault)
{
    CaptionMap map;
    const std::string json = R"({
        "no_dur.wav": { "text": "t" }
    })";
    ASSERT_TRUE(map.loadFromString(json));
    EXPECT_FLOAT_EQ(map.lookup("no_dur.wav")->durationSeconds,
                    DEFAULT_CAPTION_DURATION_SECONDS);
}

TEST(CaptionMap, NonPositiveDurationFallsBackToDefault)
{
    CaptionMap map;
    const std::string json = R"({
        "zero.wav":  { "text": "t", "duration": 0.0 },
        "neg.wav":   { "text": "t", "duration": -4.0 }
    })";
    ASSERT_TRUE(map.loadFromString(json));
    EXPECT_FLOAT_EQ(map.lookup("zero.wav")->durationSeconds,
                    DEFAULT_CAPTION_DURATION_SECONDS);
    EXPECT_FLOAT_EQ(map.lookup("neg.wav")->durationSeconds,
                    DEFAULT_CAPTION_DURATION_SECONDS);
}

TEST(CaptionMap, EntriesWithEmptyTextAreSkipped)
{
    CaptionMap map;
    const std::string json = R"({
        "empty.wav":    { "text": "" },
        "missing.wav":  { "category": "Dialogue" }
    })";
    ASSERT_TRUE(map.loadFromString(json));
    EXPECT_TRUE(map.empty());
}

TEST(CaptionMap, MalformedJsonLeavesMapEmpty)
{
    CaptionMap map;
    EXPECT_FALSE(map.loadFromString("{ not valid json"));
    EXPECT_TRUE(map.empty());
}

TEST(CaptionMap, NonObjectRootLeavesMapEmpty)
{
    CaptionMap map;
    EXPECT_FALSE(map.loadFromString("[\"array\", \"of\", \"strings\"]"));
    EXPECT_TRUE(map.empty());
}

TEST(CaptionMap, ReloadClearsPreviousEntries)
{
    CaptionMap map;
    ASSERT_TRUE(map.loadFromString(R"({ "a.wav": { "text": "t" } })"));
    EXPECT_EQ(map.size(), 1u);
    ASSERT_TRUE(map.loadFromString(R"({ "b.wav": { "text": "t" } })"));
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.lookup("a.wav"), nullptr);
    EXPECT_NE(map.lookup("b.wav"), nullptr);
}

TEST(CaptionMap, EnqueueForUnknownClipIsNoOp)
{
    CaptionMap map;
    SubtitleQueue queue;
    EXPECT_FALSE(map.enqueueFor("unknown.wav", queue));
    EXPECT_TRUE(queue.activeSubtitles().empty());
}

TEST(CaptionMap, EnqueueForMappedClipPushesCaptionOntoQueue)
{
    CaptionMap map;
    ASSERT_TRUE(map.loadFromString(R"({
        "hello.wav": {
            "category": "Dialogue",
            "speaker":  "Speaker",
            "text":     "Hello there.",
            "duration": 2.0
        }
    })"));
    SubtitleQueue queue;
    EXPECT_TRUE(map.enqueueFor("hello.wav", queue));
    ASSERT_EQ(queue.activeSubtitles().size(), 1u);
    EXPECT_EQ(queue.activeSubtitles().front().subtitle.speaker, "Speaker");
    EXPECT_EQ(queue.activeSubtitles().front().subtitle.text, "Hello there.");
}

TEST(CaptionMap, ClearEmptiesEverything)
{
    CaptionMap map;
    ASSERT_TRUE(map.loadFromString(R"({ "a.wav": { "text": "t" } })"));
    EXPECT_FALSE(map.empty());
    map.clear();
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.lookup("a.wav"), nullptr);
}

TEST(CaptionMapCategory, ParseKnownValues)
{
    EXPECT_EQ(parseSubtitleCategory("Dialogue"), SubtitleCategory::Dialogue);
    EXPECT_EQ(parseSubtitleCategory("Narrator"), SubtitleCategory::Narrator);
    EXPECT_EQ(parseSubtitleCategory("SoundCue"), SubtitleCategory::SoundCue);
}

TEST(CaptionMapCategory, ParseUnknownDefaultsToDialogue)
{
    EXPECT_EQ(parseSubtitleCategory(""),       SubtitleCategory::Dialogue);
    EXPECT_EQ(parseSubtitleCategory("other"),  SubtitleCategory::Dialogue);
}
