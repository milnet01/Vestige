// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_loudness.cpp
/// @brief Phase 10 audio quick-wins (AX9) — coverage for EBU R128 /
///        ITU-R BS.1770 loudness normalisation. The pure makeup-gain
///        math and the libebur128 measurement path both run headless
///        (libebur128 is pure CPU — no audio device needed); the
///        AudioEngine accessors + the unmeasured/disabled makeup lookup
///        are exercised on a default-constructed engine.

#include <gtest/gtest.h>

#include "audio/audio_engine.h"
#include "audio/audio_loudness.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace Vestige;

namespace
{
constexpr double kPi = 3.14159265358979323846;

/// @brief A 1 kHz sine of relative amplitude @a amp (0..1 of full scale),
///        interleaved stereo int16 at @a rate Hz for @a seconds seconds.
std::vector<int16_t> makeSine(float amp, int seconds = 2, int rate = 48000)
{
    const int frames = rate * seconds;
    std::vector<int16_t> pcm(static_cast<std::size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const double s = std::sin(2.0 * kPi * 1000.0 * t) * static_cast<double>(amp);
        const auto v = static_cast<int16_t>(std::lround(s * 32767.0));
        pcm[static_cast<std::size_t>(i) * 2]     = v;
        pcm[static_cast<std::size_t>(i) * 2 + 1] = v;
    }
    return pcm;
}
} // namespace

// ---------------------------------------------------------------------------
// Pure makeup-gain math (no libebur128, no device)
// ---------------------------------------------------------------------------

TEST(AudioLoudness, TargetEqualsMeasuredIsUnity)
{
    EXPECT_NEAR(loudnessMakeupGain(-16.0f, -16.0f), 1.0f, 1e-4f);
    EXPECT_NEAR(loudnessMakeupGain(-9.1f, -9.1f), 1.0f, 1e-4f);
}

TEST(AudioLoudness, QuietClipIsBoostedByTheDbDelta)
{
    // −20 → −14 LUFS is +6 dB ⇒ ×10^(6/20) ≈ 1.9953.
    EXPECT_NEAR(loudnessMakeupGain(-20.0f, -14.0f), 1.99526f, 1e-3f);
}

TEST(AudioLoudness, LoudClipIsAttenuated)
{
    // −6 → −16 LUFS is −10 dB ⇒ ×10^(-10/20) ≈ 0.3162, and < 1.
    const float g = loudnessMakeupGain(-6.0f, -16.0f);
    EXPECT_NEAR(g, 0.31623f, 1e-3f);
    EXPECT_LT(g, 1.0f);
}

TEST(AudioLoudness, BoostIsClampedToMaxBoostDb)
{
    // −60 → −16 would be +44 dB; clamp at +12 dB ⇒ ×10^(12/20) ≈ 3.9811,
    // NOT the unclamped 10^(44/20) ≈ 158.
    const float g = loudnessMakeupGain(-60.0f, -16.0f, 12.0f);
    EXPECT_NEAR(g, 3.98107f, 1e-2f);
    EXPECT_LT(g, 4.0f);
}

TEST(AudioLoudness, SilenceGateNeverAmplifies)
{
    // At / below the −70 LUFS absolute gate, and non-finite, → unity.
    EXPECT_EQ(loudnessMakeupGain(kLoudnessSilenceGateLufs, -16.0f), 1.0f);
    EXPECT_EQ(loudnessMakeupGain(-100.0f, -16.0f), 1.0f);
    EXPECT_EQ(loudnessMakeupGain(
                  -std::numeric_limits<float>::infinity(), -16.0f),
              1.0f);
}

// ---------------------------------------------------------------------------
// libebur128 measurement path (pure CPU — runs headless)
// ---------------------------------------------------------------------------

TEST(AudioLoudness, MeasuresFiniteLoudnessForASine)
{
    const auto pcm = makeSine(0.5f);
    const float lufs = integratedLoudnessLufs(
        pcm.data(), pcm.size() / 2, 2, 48000);
    EXPECT_TRUE(std::isfinite(lufs));
    // A moderate-level sine sits well inside the audible LUFS band.
    EXPECT_GT(lufs, -40.0f);
    EXPECT_LT(lufs, 0.0f);
}

TEST(AudioLoudness, HalvingAmplitudeDropsLoudnessBySixDb)
{
    // Self-calibrating: a −6 dB amplitude change is a −6 LUFS change,
    // independent of the absolute calibration of either reading.
    const auto loudPcm  = makeSine(0.5f);
    const auto quietPcm = makeSine(0.25f);
    const float loud  = integratedLoudnessLufs(loudPcm.data(),  loudPcm.size() / 2,  2, 48000);
    const float quiet = integratedLoudnessLufs(quietPcm.data(), quietPcm.size() / 2, 2, 48000);
    EXPECT_NEAR(loud - quiet, 6.0f, 0.5f);
}

TEST(AudioLoudness, MeasureThenMakeupRoundTripsToTheTarget)
{
    // Measuring a clip then asking for the makeup toward its own measured
    // loudness yields unity (no change) — and toward target+6 dB yields ~2×.
    const auto pcm = makeSine(0.5f);
    const float lufs = integratedLoudnessLufs(pcm.data(), pcm.size() / 2, 2, 48000);
    EXPECT_NEAR(loudnessMakeupGain(lufs, lufs), 1.0f, 1e-4f);
    EXPECT_NEAR(loudnessMakeupGain(lufs, lufs + 6.0f), 1.99526f, 1e-3f);
}

TEST(AudioLoudness, SilentAndEmptyInputAreGated)
{
    std::vector<int16_t> silence(48000 * 2, 0);  // 1 s stereo of zeros
    EXPECT_LE(integratedLoudnessLufs(silence.data(), silence.size() / 2, 2, 48000),
              kLoudnessSilenceGateLufs);
    // Empty / invalid input returns the gate (→ unity makeup).
    EXPECT_EQ(integratedLoudnessLufs(nullptr, 0, 2, 48000),
              kLoudnessSilenceGateLufs);
    EXPECT_EQ(integratedLoudnessLufs(silence.data(), 0, 2, 48000),
              kLoudnessSilenceGateLufs);
}

// ---------------------------------------------------------------------------
// AudioEngine accessors + makeup lookup (headless, never initialised)
// ---------------------------------------------------------------------------

TEST(AudioLoudness, EngineDefaultsAreOnAtMinusSixteen)
{
    AudioEngine engine;  // never initialised — no audio device
    EXPECT_TRUE(engine.isLoudnessEnabled());
    EXPECT_FLOAT_EQ(engine.getLoudnessTargetLufs(), -16.0f);

    engine.setLoudnessTargetLufs(-23.0f);
    EXPECT_FLOAT_EQ(engine.getLoudnessTargetLufs(), -23.0f);
    engine.setLoudnessEnabled(false);
    EXPECT_FALSE(engine.isLoudnessEnabled());
}

TEST(AudioLoudness, UnmeasuredOrDisabledPathReturnsUnityMakeup)
{
    AudioEngine engine;
    // No clip was ever loaded → no cached measurement → unity.
    EXPECT_FLOAT_EQ(engine.loudnessMakeupForPath("never_loaded.wav"), 1.0f);
    // Disabling forces unity regardless of any future measurement.
    engine.setLoudnessEnabled(false);
    EXPECT_FLOAT_EQ(engine.loudnessMakeupForPath("anything.wav"), 1.0f);
}
