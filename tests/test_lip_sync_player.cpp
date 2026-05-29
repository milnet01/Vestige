// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_lip_sync_player.cpp
/// @brief Unit tests for LipSyncPlayer track loading, playback, amplitude mode, and clone.
#include "experimental/animation/lip_sync.h"
#include "experimental/animation/facial_animation.h"
#include "experimental/animation/facial_presets.h"
#include "animation/skeleton_animator.h"
#include "experimental/animation/viseme_map.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Vestige;

// Wire `animator` + `skelAnimator` for the 13 ARKit shapes used by the
// LipSyncPlayer / LipSyncAmplitude fixtures. Returns the shape count.
static int setupLipSyncPipeline(FacialAnimator& animator, SkeletonAnimator& skelAnimator)
{
    animator.setAnimator(&skelAnimator);

    std::vector<std::string> shapes = {
        "jawOpen", "mouthClose", "mouthPressLeft", "mouthPressRight",
        "mouthStretchLeft", "mouthStretchRight",
        "mouthLowerDownLeft", "mouthLowerDownRight",
        "mouthFunnel", "mouthPucker",
        "mouthUpperUpLeft", "mouthUpperUpRight",
        "tongueOut"
    };
    animator.mapBlendShapes(shapes);
    skelAnimator.setMorphTargetCount(static_cast<int>(shapes.size()));
    return static_cast<int>(shapes.size());
}

class LipSyncTrackTest : public ::testing::Test
{
protected:
    LipSyncPlayer player;

    static constexpr const char* SAMPLE_JSON = R"({
        "metadata": {
            "soundFile": "test.wav",
            "duration": 1.5
        },
        "mouthCues": [
            {"start": 0.00, "end": 0.05, "value": "X"},
            {"start": 0.05, "end": 0.27, "value": "D"},
            {"start": 0.27, "end": 0.31, "value": "C"},
            {"start": 0.31, "end": 0.43, "value": "B"},
            {"start": 0.43, "end": 0.50, "value": "F"},
            {"start": 0.50, "end": 0.80, "value": "C"},
            {"start": 0.80, "end": 1.00, "value": "A"},
            {"start": 1.00, "end": 1.50, "value": "X"}
        ]
    })";

    static constexpr const char* SAMPLE_TSV =
        "0.00\tX\n"
        "0.05\tD\n"
        "0.27\tC\n"
        "0.31\tB\n"
        "0.43\tF\n"
        "0.50\tC\n"
        "0.80\tA\n"
        "1.00\tX\n";
};

class LipSyncPlayerTest : public ::testing::Test
{
protected:
    LipSyncPlayer player;
    FacialAnimator animator;
    SkeletonAnimator skelAnimator;

    void SetUp() override
    {
        setupLipSyncPipeline(animator, skelAnimator);

        player.setFacialAnimator(&animator);

        // Load sample track
        player.loadTrackFromString(R"({
            "mouthCues": [
                {"start": 0.0, "end": 0.5, "value": "X"},
                {"start": 0.5, "end": 1.0, "value": "D"},
                {"start": 1.0, "end": 1.5, "value": "A"},
                {"start": 1.5, "end": 2.0, "value": "X"}
            ]
        })");
    }
};

class LipSyncAmplitudeTest : public ::testing::Test
{
protected:
    LipSyncPlayer player;
    FacialAnimator animator;
    SkeletonAnimator skelAnimator;

    void SetUp() override
    {
        setupLipSyncPipeline(animator, skelAnimator);

        player.setFacialAnimator(&animator);
        player.enableAmplitudeMode();
    }
};

TEST_F(LipSyncTrackTest, LoadJSON)
{
    ASSERT_TRUE(player.loadTrackFromString(SAMPLE_JSON));
    const auto* track = player.getTrack();
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->cues.size(), 8u);
    EXPECT_FLOAT_EQ(track->duration, 1.5f);
    EXPECT_EQ(track->soundFile, "test.wav");
}

TEST_F(LipSyncTrackTest, JSONCueValues)
{
    player.loadTrackFromString(SAMPLE_JSON);
    const auto* track = player.getTrack();

    EXPECT_EQ(track->cues[0].viseme, Viseme::X);
    EXPECT_FLOAT_EQ(track->cues[0].start, 0.0f);
    EXPECT_FLOAT_EQ(track->cues[0].end, 0.05f);

    EXPECT_EQ(track->cues[1].viseme, Viseme::D);
    EXPECT_FLOAT_EQ(track->cues[1].start, 0.05f);

    EXPECT_EQ(track->cues[6].viseme, Viseme::A);
}

TEST_F(LipSyncTrackTest, LoadTSV)
{
    ASSERT_TRUE(player.loadTrackFromTSV(SAMPLE_TSV));
    const auto* track = player.getTrack();
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->cues.size(), 8u);
    EXPECT_GT(track->duration, 1.0f);
}

TEST_F(LipSyncTrackTest, TSVCueValues)
{
    player.loadTrackFromTSV(SAMPLE_TSV);
    const auto* track = player.getTrack();

    EXPECT_EQ(track->cues[0].viseme, Viseme::X);
    EXPECT_FLOAT_EQ(track->cues[0].start, 0.0f);
    EXPECT_FLOAT_EQ(track->cues[0].end, 0.05f);

    EXPECT_EQ(track->cues[1].viseme, Viseme::D);
}

TEST_F(LipSyncTrackTest, InvalidJSONFails)
{
    EXPECT_FALSE(player.loadTrackFromString("not json"));
}

TEST_F(LipSyncTrackTest, MissingMouthCuesFails)
{
    EXPECT_FALSE(player.loadTrackFromString(R"({"metadata": {}})"));
}

TEST_F(LipSyncTrackTest, EmptyTSVFails)
{
    EXPECT_FALSE(player.loadTrackFromTSV(""));
}

TEST_F(LipSyncTrackTest, TSVBlankRowsAreSkipped_CV13)
{
    // Blank lines between cues are tolerated (skipped), not treated as
    // parse errors — the surrounding valid cues still load.
    ASSERT_TRUE(player.loadTrackFromTSV("0.00\tX\n\n0.50\tD\n\n"));
    const auto* track = player.getTrack();
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->cues.size(), 2u);
}

TEST_F(LipSyncTrackTest, TSVNonNumericTimestampRowsAreSkipped_CV13)
{
    // A row whose first column is not a number fails the `>> time`
    // extraction and is dropped; a bad row between two good rows leaves 2.
    ASSERT_TRUE(player.loadTrackFromTSV("0.00\tX\nNOT_A_TIME\tD\n0.50\tF\n"));
    const auto* track = player.getTrack();
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->cues.size(), 2u);

    // A TSV with no numeric rows at all yields zero cues → load fails.
    EXPECT_FALSE(player.loadTrackFromTSV("alpha\tX\nbeta\tD\n"));
}

TEST_F(LipSyncTrackTest, TSVNegativeTimesAreAcceptedPermissively_CV13)
{
    // The parser does NOT validate the sign of the timestamp — a negative
    // start time passes through unchanged. Pins the current (permissive)
    // behaviour: Rhubarb never emits negatives, but a hand-edited file
    // could. Flip to EXPECT_FALSE if non-negative validation is ever added.
    ASSERT_TRUE(player.loadTrackFromTSV("-1.00\tX\n0.50\tD\n"));
    const auto* track = player.getTrack();
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->cues.size(), 2u);
    EXPECT_FLOAT_EQ(track->cues[0].start, -1.0f);
}

TEST_F(LipSyncTrackTest, NoTrackReturnsNull)
{
    EXPECT_EQ(player.getTrack(), nullptr);
}

TEST_F(LipSyncPlayerTest, InitialState)
{
    EXPECT_EQ(player.getState(), LipSyncState::STOPPED);
    EXPECT_FLOAT_EQ(player.getTime(), 0.0f);
    EXPECT_FALSE(player.isLooping());
    EXPECT_FALSE(player.isAmplitudeMode());
}

TEST_F(LipSyncPlayerTest, PlayPauseStop)
{
    player.play();
    EXPECT_EQ(player.getState(), LipSyncState::PLAYING);

    player.pause();
    EXPECT_EQ(player.getState(), LipSyncState::PAUSED);

    player.play();
    EXPECT_EQ(player.getState(), LipSyncState::PLAYING);

    player.stop();
    EXPECT_EQ(player.getState(), LipSyncState::STOPPED);
    EXPECT_FLOAT_EQ(player.getTime(), 0.0f);
}

TEST_F(LipSyncPlayerTest, TimeAdvances)
{
    player.play();
    player.update(0.1f);
    EXPECT_NEAR(player.getTime(), 0.1f, 0.001f);

    player.update(0.2f);
    EXPECT_NEAR(player.getTime(), 0.3f, 0.001f);
}

TEST_F(LipSyncPlayerTest, PauseStopsTimeAdvance)
{
    player.play();
    player.update(0.1f);
    float t = player.getTime();

    player.pause();
    player.update(0.5f);
    EXPECT_FLOAT_EQ(player.getTime(), t);
}

TEST_F(LipSyncPlayerTest, StopsAtEnd)
{
    player.play();
    player.update(3.0f);  // Past the 2.0s track duration
    EXPECT_EQ(player.getState(), LipSyncState::STOPPED);
}

TEST_F(LipSyncPlayerTest, LoopsAtEnd)
{
    player.setLooping(true);
    player.play();
    player.update(2.5f);  // Past the 2.0s track
    EXPECT_EQ(player.getState(), LipSyncState::PLAYING);
    EXPECT_LT(player.getTime(), 2.0f);  // Should have wrapped
}

TEST_F(LipSyncPlayerTest, SetTime)
{
    player.setTime(0.75f);
    EXPECT_FLOAT_EQ(player.getTime(), 0.75f);
}

TEST_F(LipSyncPlayerTest, SmoothingDefault)
{
    EXPECT_NEAR(player.getSmoothing(), 0.15f, 0.001f);
}

TEST_F(LipSyncPlayerTest, SmoothingClamped)
{
    player.setSmoothing(2.0f);
    EXPECT_FLOAT_EQ(player.getSmoothing(), 1.0f);

    player.setSmoothing(-1.0f);
    EXPECT_FLOAT_EQ(player.getSmoothing(), 0.0f);
}

TEST_F(LipSyncPlayerTest, WeightsAppliedDuringWideOpenCue)
{
    // Set high smoothing for instant response
    player.setSmoothing(1.0f);
    player.setTime(0.6f);  // Middle of D (wide open) cue
    player.play();
    player.update(0.01f);
    animator.update(0.01f);

    // jawOpen should have been set (D viseme has jawOpen = 0.7)
    const auto& weights = skelAnimator.getMorphWeights();
    // Index 0 is jawOpen in our shape list
    EXPECT_GT(weights[0], 0.3f) << "jawOpen should be > 0.3 during D viseme";
}

TEST_F(LipSyncPlayerTest, WeightsAppliedDuringClosedCue)
{
    player.setSmoothing(1.0f);
    player.setTime(1.1f);  // Middle of A (closed) cue
    player.play();
    player.update(0.01f);
    animator.update(0.01f);

    // mouthClose should have been set (A viseme has mouthClose = 0.8)
    const auto& weights = skelAnimator.getMorphWeights();
    // Index 1 is mouthClose in our shape list
    EXPECT_GT(weights[1], 0.3f) << "mouthClose should be > 0.3 during A viseme";
}

TEST_F(LipSyncPlayerTest, RestCueProducesMinimalWeights)
{
    player.setSmoothing(1.0f);
    player.setTime(0.1f);  // Middle of X (rest) cue
    player.play();
    player.update(0.01f);
    animator.update(0.01f);

    // All weights should be near zero for rest viseme
    const auto& weights = skelAnimator.getMorphWeights();
    for (size_t i = 0; i < weights.size(); ++i)
    {
        EXPECT_LT(weights[i], 0.05f) << "Weight " << i << " should be near zero during rest";
    }
}

TEST_F(LipSyncAmplitudeTest, AmplitudeModeEnabled)
{
    EXPECT_TRUE(player.isAmplitudeMode());
}

TEST_F(LipSyncAmplitudeTest, DisableAmplitudeMode)
{
    player.disableAmplitudeMode();
    EXPECT_FALSE(player.isAmplitudeMode());
}

TEST_F(LipSyncAmplitudeTest, SilenceProducesNoWeight)
{
    player.setSmoothing(1.0f);
    std::vector<float> silence(1024, 0.0f);
    player.feedAudioSamples(silence.data(), silence.size(), 44100);
    player.update(0.016f);
    animator.update(0.016f);

    const auto& weights = skelAnimator.getMorphWeights();
    for (size_t i = 0; i < weights.size(); ++i)
    {
        EXPECT_LT(weights[i], 0.05f);
    }
}

TEST_F(LipSyncAmplitudeTest, LoudSignalProducesMouthMovement)
{
    player.setSmoothing(1.0f);
    // Generate a loud 440 Hz sine wave
    std::vector<float> loud(1024);
    for (size_t i = 0; i < loud.size(); ++i)
    {
        loud[i] = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f *
                                   static_cast<float>(i) / 44100.0f);
    }
    player.feedAudioSamples(loud.data(), loud.size(), 44100);
    player.update(0.016f);
    animator.update(0.016f);

    // At least one mouth weight should be non-zero
    const auto& weights = skelAnimator.getMorphWeights();
    float maxWeight = 0.0f;
    for (float w : weights)
    {
        maxWeight = std::max(maxWeight, w);
    }
    EXPECT_GT(maxWeight, 0.05f) << "Loud audio should produce some mouth movement";
}

TEST_F(LipSyncAmplitudeTest, AnalyzerAccessible)
{
    const auto& analyzer = player.getAnalyzer();
    EXPECT_FLOAT_EQ(analyzer.getRMS(), 0.0f);  // No samples fed yet
}

TEST(LipSyncCloneTest, ClonePreservesTrack)
{
    LipSyncPlayer original;
    original.loadTrackFromString(R"({
        "mouthCues": [
            {"start": 0.0, "end": 1.0, "value": "D"}
        ]
    })");
    original.setSmoothing(0.5f);
    original.setLooping(true);

    auto cloned = original.clone();
    auto* clonedPlayer = dynamic_cast<LipSyncPlayer*>(cloned.get());
    ASSERT_NE(clonedPlayer, nullptr);

    EXPECT_NE(clonedPlayer->getTrack(), nullptr);
    EXPECT_EQ(clonedPlayer->getTrack()->cues.size(), 1u);
    EXPECT_FLOAT_EQ(clonedPlayer->getSmoothing(), 0.5f);
    EXPECT_TRUE(clonedPlayer->isLooping());

    // Playback state should NOT be cloned
    EXPECT_EQ(clonedPlayer->getState(), LipSyncState::STOPPED);
}
