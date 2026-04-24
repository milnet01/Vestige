// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_caption_map.cpp
/// @brief Phase 10.7 slice B3 — declarative caption map coverage.

#include <gtest/gtest.h>

#include "audio/audio_engine.h"
#include "ui/caption_map.h"
#include "ui/subtitle.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

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

// =============================================================================
// Phase 10.9 Slice 2 P4 — Caption auto-enqueue announcer on playSound*
// =============================================================================
//
// Captions don't self-enqueue. Phase 10.7 slice B3 added CaptionMap::enqueueFor
// but nothing called it at clip-play time — the design doc's "When an
// AudioSourceComponent plays a clip with a matching key, the audio system
// auto-enqueues a caption" promise had no wire. These tests pin the
// callback contract on AudioEngine so every playSound* overload announces
// the clip path at entry, before the availability check — users with no
// audio hardware / deafness still see the caption when game code intends
// to play a sound.

TEST(AudioEngineCaptionAnnouncer, FiresOnPlaySound_P4)
{
    AudioEngine engine;  // default-constructed, no initialize() — m_available stays false
    std::vector<std::string> captured;
    engine.setCaptionAnnouncer([&](const std::string& clip)
    {
        captured.push_back(clip);
    });

    engine.playSound("audio/dialogue/moses_01.wav", glm::vec3(0.0f));

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], "audio/dialogue/moses_01.wav");
}

TEST(AudioEngineCaptionAnnouncer, FiresOnPlaySoundSpatialAttenParams_P4)
{
    AudioEngine engine;
    std::vector<std::string> captured;
    engine.setCaptionAnnouncer([&](const std::string& clip)
    {
        captured.push_back(clip);
    });

    AttenuationParams params;
    engine.playSoundSpatial(
        "audio/fx/footstep.wav", glm::vec3(0.0f), params);

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], "audio/fx/footstep.wav");
}

TEST(AudioEngineCaptionAnnouncer, FiresOnPlaySoundSpatialWithVelocity_P4)
{
    AudioEngine engine;
    std::vector<std::string> captured;
    engine.setCaptionAnnouncer([&](const std::string& clip)
    {
        captured.push_back(clip);
    });

    AttenuationParams params;
    engine.playSoundSpatial(
        "audio/fx/arrow_whoosh.wav",
        glm::vec3(0.0f),
        glm::vec3(10.0f, 0.0f, 0.0f),
        params);

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], "audio/fx/arrow_whoosh.wav");
}

TEST(AudioEngineCaptionAnnouncer, FiresOnPlaySound2D_P4)
{
    AudioEngine engine;
    std::vector<std::string> captured;
    engine.setCaptionAnnouncer([&](const std::string& clip)
    {
        captured.push_back(clip);
    });

    engine.playSound2D("audio/ui/menu_accept.wav");

    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], "audio/ui/menu_accept.wav");
}

TEST(AudioEngineCaptionAnnouncer, FiresOncePerPlayCall_P4)
{
    // Exactly one announcement per playSound call — the roadmap
    // specifies "fires once at source-acquire, not every frame".
    AudioEngine engine;
    std::vector<std::string> captured;
    engine.setCaptionAnnouncer([&](const std::string& clip)
    {
        captured.push_back(clip);
    });

    engine.playSound("clip_a.wav", glm::vec3(0.0f));
    engine.playSound("clip_b.wav", glm::vec3(0.0f));
    engine.playSound("clip_a.wav", glm::vec3(0.0f));

    ASSERT_EQ(captured.size(), 3u);
    EXPECT_EQ(captured[0], "clip_a.wav");
    EXPECT_EQ(captured[1], "clip_b.wav");
    EXPECT_EQ(captured[2], "clip_a.wav");
}

TEST(AudioEngineCaptionAnnouncer, NoAnnouncerIsSafe_P4)
{
    // Defensive: an engine that never had an announcer installed must
    // not crash when playSound* is called. The announcer is a
    // purely-optional hook.
    AudioEngine engine;
    EXPECT_NO_THROW(engine.playSound("anything.wav", glm::vec3(0.0f)));
    EXPECT_NO_THROW(engine.playSound2D("anything.wav"));
}

TEST(AudioEngineCaptionAnnouncer, CaptionMapIntegrationEnqueuesMappedClip_P4)
{
    // End-to-end: production wiring is
    // engine.setCaptionAnnouncer([map, queue](clip){ map.enqueueFor(clip, queue); }).
    // Assert that playing a mapped clip path produces a caption in the queue.
    CaptionMap map;
    const std::string json = R"({
        "audio/dialogue/moses_01.wav": {
            "category": "Dialogue",
            "speaker":  "Moses",
            "text":     "Draw near.",
            "duration": 2.5
        }
    })";
    ASSERT_TRUE(map.loadFromString(json));

    SubtitleQueue queue;
    AudioEngine engine;
    engine.setCaptionAnnouncer([&](const std::string& clip)
    {
        map.enqueueFor(clip, queue);
    });

    engine.playSound("audio/dialogue/moses_01.wav", glm::vec3(0.0f));
    ASSERT_EQ(queue.size(), 1u);
    EXPECT_EQ(queue.activeSubtitles()[0].subtitle.speaker, "Moses");
    EXPECT_EQ(queue.activeSubtitles()[0].subtitle.text,    "Draw near.");
}

TEST(AudioEngineCaptionAnnouncer, UnmappedClipEnqueuesNothing_P4)
{
    // Negative: clips without a map entry produce no caption — the
    // CaptionMap::enqueueFor no-op passes through the announcer.
    CaptionMap map;
    SubtitleQueue queue;
    AudioEngine engine;
    engine.setCaptionAnnouncer([&](const std::string& clip)
    {
        map.enqueueFor(clip, queue);
    });

    engine.playSound("audio/fx/unmapped.wav", glm::vec3(0.0f));
    EXPECT_EQ(queue.size(), 0u);
}
