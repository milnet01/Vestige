// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_lip_sync.cpp
/// @brief Unit tests for viseme mapping, audio analysis, and lip sync playback.

#include "experimental/animation/audio_analyzer.h"
#include "experimental/animation/facial_animation.h"
#include "experimental/animation/facial_presets.h"
#include "experimental/animation/lip_sync.h"
#include "animation/skeleton_animator.h"
#include "experimental/animation/viseme_map.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Vestige;

// ===========================================================================
// VisemeMap tests
// ===========================================================================

class VisemeMapTest : public ::testing::Test {};

TEST_F(VisemeMapTest, AllVisemesExist)
{
    for (int i = 0; i < static_cast<int>(Viseme::COUNT); ++i)
    {
        auto viseme = static_cast<Viseme>(i);
        const auto& shape = VisemeMap::get(viseme);
        EXPECT_EQ(shape.viseme, viseme);
    }
}

TEST_F(VisemeMapTest, AllVisemesHaveNames)
{
    for (int i = 0; i < static_cast<int>(Viseme::COUNT); ++i)
    {
        auto viseme = static_cast<Viseme>(i);
        const char* name = visemeName(viseme);
        EXPECT_NE(name, nullptr);
        EXPECT_GT(strlen(name), 0u);
    }
}

TEST_F(VisemeMapTest, RestVisemeHasNoEntries)
{
    const auto& rest = VisemeMap::get(Viseme::X);
    EXPECT_TRUE(rest.entries.empty());
}

TEST_F(VisemeMapTest, NonRestVisemesHaveEntries)
{
    for (int i = 1; i < static_cast<int>(Viseme::COUNT); ++i)
    {
        auto viseme = static_cast<Viseme>(i);
        const auto& shape = VisemeMap::get(viseme);
        EXPECT_FALSE(shape.entries.empty())
            << "Viseme " << visemeName(viseme) << " has no entries";
    }
}

TEST_F(VisemeMapTest, AllWeightsInValidRange)
{
    for (int i = 0; i < static_cast<int>(Viseme::COUNT); ++i)
    {
        auto viseme = static_cast<Viseme>(i);
        for (const auto& entry : VisemeMap::get(viseme).entries)
        {
            EXPECT_GE(entry.weight, 0.0f);
            EXPECT_LE(entry.weight, 1.0f);
        }
    }
}

TEST_F(VisemeMapTest, RhubarbCharRoundtrip)
{
    const char chars[] = {'X', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    for (char c : chars)
    {
        Viseme v = VisemeMap::fromRhubarbChar(c);
        char result = VisemeMap::toRhubarbChar(v);
        EXPECT_EQ(result, c) << "Roundtrip failed for char '" << c << "'";
    }
}

TEST_F(VisemeMapTest, RhubarbCharCaseInsensitive)
{
    EXPECT_EQ(VisemeMap::fromRhubarbChar('a'), Viseme::A);
    EXPECT_EQ(VisemeMap::fromRhubarbChar('A'), Viseme::A);
    EXPECT_EQ(VisemeMap::fromRhubarbChar('x'), Viseme::X);
}

TEST_F(VisemeMapTest, UnknownCharDefaultsToRest)
{
    EXPECT_EQ(VisemeMap::fromRhubarbChar('Z'), Viseme::X);
    EXPECT_EQ(VisemeMap::fromRhubarbChar('1'), Viseme::X);
    EXPECT_EQ(VisemeMap::fromRhubarbChar('\0'), Viseme::X);
}

TEST_F(VisemeMapTest, OutOfRangeVisemeFallsBackToRest)
{
    const auto& shape = VisemeMap::get(static_cast<Viseme>(255));
    EXPECT_EQ(shape.viseme, Viseme::X);
}

TEST_F(VisemeMapTest, BlendWeightsAtZero)
{
    std::unordered_map<std::string, float> out;
    VisemeMap::blendWeights(Viseme::A, Viseme::D, 0.0f, out);

    // Should be pure A
    const auto& shapeA = VisemeMap::get(Viseme::A);
    for (const auto& entry : shapeA.entries)
    {
        EXPECT_NEAR(out[entry.shapeName], entry.weight, 0.001f);
    }
}

TEST_F(VisemeMapTest, BlendWeightsAtOne)
{
    std::unordered_map<std::string, float> out;
    VisemeMap::blendWeights(Viseme::A, Viseme::D, 1.0f, out);

    // Should be pure D
    const auto& shapeD = VisemeMap::get(Viseme::D);
    for (const auto& entry : shapeD.entries)
    {
        EXPECT_NEAR(out[entry.shapeName], entry.weight, 0.001f);
    }
}

TEST_F(VisemeMapTest, BlendWeightsAtHalf)
{
    std::unordered_map<std::string, float> out;
    VisemeMap::blendWeights(Viseme::A, Viseme::D, 0.5f, out);

    // jawOpen should be 0.5 * (A's jawOpen) + 0.5 * (D's jawOpen)
    // A has no jawOpen, D has jawOpen = 0.7
    EXPECT_NEAR(out["jawOpen"], 0.35f, 0.001f);
}

TEST_F(VisemeMapTest, BlendWithRestProducesScaledWeights)
{
    std::unordered_map<std::string, float> out;
    VisemeMap::blendWeights(Viseme::D, Viseme::X, 0.5f, out);

    // D has jawOpen = 0.7, blended 50% with rest (0) = 0.35
    EXPECT_NEAR(out["jawOpen"], 0.35f, 0.001f);
}

TEST_F(VisemeMapTest, GetCountMatchesEnumSize)
{
    EXPECT_EQ(VisemeMap::getCount(), static_cast<int>(Viseme::COUNT));
}

TEST_F(VisemeMapTest, ClosedVisemeHasMouthClose)
{
    // Viseme A (P/B/M) should have mouthClose
    const auto& shapeA = VisemeMap::get(Viseme::A);
    bool found = false;
    for (const auto& entry : shapeA.entries)
    {
        if (std::string(entry.shapeName) == "mouthClose")
        {
            found = true;
            EXPECT_GT(entry.weight, 0.5f);
        }
    }
    EXPECT_TRUE(found) << "Viseme A should have mouthClose";
}

TEST_F(VisemeMapTest, WideOpenVisemeHasLargeJawOpen)
{
    // Viseme D (AA) should have jawOpen > 0.5
    const auto& shapeD = VisemeMap::get(Viseme::D);
    bool found = false;
    for (const auto& entry : shapeD.entries)
    {
        if (std::string(entry.shapeName) == "jawOpen")
        {
            found = true;
            EXPECT_GT(entry.weight, 0.5f);
        }
    }
    EXPECT_TRUE(found) << "Viseme D should have jawOpen > 0.5";
}

TEST_F(VisemeMapTest, PuckerVisemeHasMouthPucker)
{
    // Viseme F (UW/OW/W) should have mouthPucker
    const auto& shapeF = VisemeMap::get(Viseme::F);
    bool found = false;
    for (const auto& entry : shapeF.entries)
    {
        if (std::string(entry.shapeName) == "mouthPucker")
        {
            found = true;
            EXPECT_GT(entry.weight, 0.5f);
        }
    }
    EXPECT_TRUE(found) << "Viseme F should have mouthPucker";
}

// ===========================================================================
// AudioAnalyzer tests
// ===========================================================================

class AudioAnalyzerTest : public ::testing::Test
{
protected:
    AudioAnalyzer analyzer;
};

TEST_F(AudioAnalyzerTest, InitialState)
{
    EXPECT_FLOAT_EQ(analyzer.getRMS(), 0.0f);
    EXPECT_FLOAT_EQ(analyzer.getSpectralCentroid(), 0.0f);
    EXPECT_FALSE(analyzer.hasSpectralData());
    EXPECT_EQ(analyzer.getEstimatedViseme(), Viseme::X);
}

TEST_F(AudioAnalyzerTest, SilenceGivesZeroRMS)
{
    std::vector<float> silence(1024, 0.0f);
    analyzer.feedSamples(silence.data(), silence.size(), 44100);
    EXPECT_FLOAT_EQ(analyzer.getRMS(), 0.0f);
    EXPECT_EQ(analyzer.getEstimatedViseme(), Viseme::X);
}

TEST_F(AudioAnalyzerTest, LoudSignalGivesHighRMS)
{
    std::vector<float> loud(1024, 0.5f);
    analyzer.feedSamples(loud.data(), loud.size(), 44100);
    EXPECT_GT(analyzer.getRMS(), 0.3f);
}

TEST_F(AudioAnalyzerTest, SineWaveRMS)
{
    // 440 Hz sine wave at full amplitude: RMS should be ~0.707
    std::vector<float> sine(1024);
    for (size_t i = 0; i < sine.size(); ++i)
    {
        sine[i] = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f *
                           static_cast<float>(i) / 44100.0f);
    }
    analyzer.feedSamples(sine.data(), sine.size(), 44100);
    EXPECT_NEAR(analyzer.getRMS(), 0.707f, 0.05f);
}

TEST_F(AudioAnalyzerTest, SilenceThreshold)
{
    analyzer.setSilenceThreshold(0.1f);
    EXPECT_FLOAT_EQ(analyzer.getSilenceThreshold(), 0.1f);

    // Very quiet signal (below threshold)
    std::vector<float> quiet(1024, 0.01f);
    analyzer.feedSamples(quiet.data(), quiet.size(), 44100);
    EXPECT_FLOAT_EQ(analyzer.getRMS(), 0.0f);
}

TEST_F(AudioAnalyzerTest, FFTWithEnoughSamples)
{
    // 512+ samples triggers FFT
    std::vector<float> signal(512);
    for (size_t i = 0; i < signal.size(); ++i)
    {
        signal[i] = std::sin(2.0f * static_cast<float>(M_PI) * 1000.0f *
                             static_cast<float>(i) / 44100.0f);
    }
    analyzer.feedSamples(signal.data(), signal.size(), 44100);
    EXPECT_TRUE(analyzer.hasSpectralData());
    EXPECT_GT(analyzer.getSpectralCentroid(), 0.0f);
}

TEST_F(AudioAnalyzerTest, FFTNotEnoughSamples)
{
    // Fewer than 512 samples — no FFT
    std::vector<float> signal(256, 0.5f);
    analyzer.feedSamples(signal.data(), signal.size(), 44100);
    EXPECT_FALSE(analyzer.hasSpectralData());
}

TEST_F(AudioAnalyzerTest, HighFrequencyHasHighCentroid)
{
    // 3000 Hz sine — should have high spectral centroid
    std::vector<float> high(1024);
    for (size_t i = 0; i < high.size(); ++i)
    {
        high[i] = std::sin(2.0f * static_cast<float>(M_PI) * 3000.0f *
                           static_cast<float>(i) / 44100.0f);
    }
    analyzer.feedSamples(high.data(), high.size(), 44100);

    AudioAnalyzer lowAnalyzer;
    // 200 Hz sine — should have low spectral centroid
    std::vector<float> low(1024);
    for (size_t i = 0; i < low.size(); ++i)
    {
        low[i] = std::sin(2.0f * static_cast<float>(M_PI) * 200.0f *
                          static_cast<float>(i) / 44100.0f);
    }
    lowAnalyzer.feedSamples(low.data(), low.size(), 44100);

    EXPECT_GT(analyzer.getSpectralCentroid(), lowAnalyzer.getSpectralCentroid());
}

TEST_F(AudioAnalyzerTest, JawOpenWeightFromAmplitude)
{
    // Silent → zero
    std::vector<float> silence(1024, 0.0f);
    analyzer.feedSamples(silence.data(), silence.size(), 44100);
    EXPECT_FLOAT_EQ(analyzer.getJawOpenWeight(), 0.0f);

    // Loud → close to 1
    std::vector<float> loud(1024, 0.4f);
    analyzer.feedSamples(loud.data(), loud.size(), 44100);
    EXPECT_GT(analyzer.getJawOpenWeight(), 0.8f);
}

TEST_F(AudioAnalyzerTest, Reset)
{
    std::vector<float> signal(1024, 0.5f);
    analyzer.feedSamples(signal.data(), signal.size(), 44100);
    EXPECT_GT(analyzer.getRMS(), 0.0f);

    analyzer.reset();
    EXPECT_FLOAT_EQ(analyzer.getRMS(), 0.0f);
    EXPECT_FALSE(analyzer.hasSpectralData());
}

TEST_F(AudioAnalyzerTest, NullSamplesHandled)
{
    analyzer.feedSamples(nullptr, 0, 44100);
    EXPECT_FLOAT_EQ(analyzer.getRMS(), 0.0f);
}

TEST_F(AudioAnalyzerTest, EstimatedVisemeVariesWithVolume)
{
    // Quiet signal → should not be wide open (D)
    std::vector<float> quiet(1024, 0.02f);
    analyzer.feedSamples(quiet.data(), quiet.size(), 44100);
    Viseme quietViseme = analyzer.getEstimatedViseme();

    // Loud signal
    AudioAnalyzer loudAnalyzer;
    std::vector<float> loud(1024, 0.5f);
    loudAnalyzer.feedSamples(loud.data(), loud.size(), 44100);
    Viseme loudViseme = loudAnalyzer.getEstimatedViseme();

    // Loud should produce a more open mouth than quiet
    // D (wide) > C (open) > B (teeth)
    EXPECT_GE(static_cast<int>(loudViseme), static_cast<int>(quietViseme));
}

// ===========================================================================
// LipSyncTrack loading tests
// ===========================================================================

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

TEST_F(LipSyncTrackTest, NoTrackReturnsNull)
{
    EXPECT_EQ(player.getTrack(), nullptr);
}

// ===========================================================================
// LipSyncPlayer playback tests
// ===========================================================================

class LipSyncPlayerTest : public ::testing::Test
{
protected:
    LipSyncPlayer player;
    FacialAnimator animator;
    SkeletonAnimator skelAnimator;

    void SetUp() override
    {
        // Set up the facial animation pipeline
        animator.setAnimator(&skelAnimator);

        // Map a subset of ARKit shapes
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

// ===========================================================================
// Amplitude mode tests
// ===========================================================================

class LipSyncAmplitudeTest : public ::testing::Test
{
protected:
    LipSyncPlayer player;
    FacialAnimator animator;
    SkeletonAnimator skelAnimator;

    void SetUp() override
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

        player.setFacialAnimator(&animator);
        player.enableAmplitudeMode();
    }
};

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

// ===========================================================================
// Clone test
// ===========================================================================

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
