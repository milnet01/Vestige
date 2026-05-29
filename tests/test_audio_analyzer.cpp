// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_analyzer.cpp
/// @brief Unit tests for the AudioAnalyzer RMS/FFT/centroid analysis.
#include "experimental/animation/audio_analyzer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace Vestige;

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
