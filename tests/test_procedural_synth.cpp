// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_procedural_synth.cpp
/// @brief Procedural synthesis-core tests (AX4 S4): modal output is finite and
///        decays; PhISEM grain count ≈ rate×duration and is deterministic under
///        an injected RNG.
#include "audio/procedural/modal_synth.h"
#include "audio/procedural/phisem.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace Vestige::Procedural;

namespace
{

ModalStrike stoneStrike()
{
    ModalStrike s;
    s.modes = {{380.0f, 34.0f, 1.0f}, {920.0f, 48.0f, 0.6f}, {1840.0f, 70.0f, 0.3f}};
    s.durSec = 0.18f;
    s.energyGain = 1.0f;
    return s;
}

/// Peak absolute amplitude over a [begin, end) sample window.
int peakAbs(const std::vector<std::int16_t>& pcm, std::size_t begin, std::size_t end)
{
    int peak = 0;
    for (std::size_t i = begin; i < end && i < pcm.size(); ++i)
    {
        peak = std::max(peak, std::abs(static_cast<int>(pcm[i])));
    }
    return peak;
}

}  // namespace

// ---------------------------------------------------------------------------
// Modal synthesis
// ---------------------------------------------------------------------------

TEST(ModalSynth, ProducesFinitePcmOfExpectedLength)
{
    std::vector<std::int16_t> pcm;
    const std::size_t n = synthesizeModal(stoneStrike(), pcm);

    EXPECT_EQ(n, pcm.size());
    EXPECT_EQ(n, static_cast<std::size_t>(0.18f * kSynthSampleRate + 0.5f));
    EXPECT_GT(peakAbs(pcm, 0, pcm.size()), 0) << "a struck solid must ring";
}

TEST(ModalSynth, EnvelopeDecays)
{
    std::vector<std::int16_t> pcm;
    synthesizeModal(stoneStrike(), pcm);
    ASSERT_FALSE(pcm.empty());

    const std::size_t q = pcm.size() / 4;
    const int firstQuarter = peakAbs(pcm, 0, q);
    const int lastQuarter = peakAbs(pcm, 3 * q, pcm.size());
    EXPECT_GT(firstQuarter, lastQuarter) << "a damped strike must decay over time";
}

TEST(ModalSynth, EnergyGainScalesLoudness)
{
    ModalStrike soft = stoneStrike();
    soft.energyGain = 0.25f;
    ModalStrike loud = stoneStrike();
    loud.energyGain = 1.0f;

    std::vector<std::int16_t> softPcm;
    std::vector<std::int16_t> loudPcm;
    synthesizeModal(soft, softPcm);
    synthesizeModal(loud, loudPcm);

    EXPECT_LT(peakAbs(softPcm, 0, softPcm.size()), peakAbs(loudPcm, 0, loudPcm.size()))
        << "a softer strike (lower energyGain) must be quieter";
}

TEST(ModalSynth, DurationIsClampedToCeiling)
{
    ModalStrike s = stoneStrike();
    s.durSec = 1.0f;  // above kMaxDurationSec
    std::vector<std::int16_t> pcm;
    const std::size_t n = synthesizeModal(s, pcm);
    EXPECT_EQ(n, static_cast<std::size_t>(kMaxDurationSec * kSynthSampleRate + 0.5f));
}

TEST(ModalSynth, EmptyModesProducesSilence)
{
    ModalStrike s;
    s.durSec = 0.1f;
    std::vector<std::int16_t> pcm;
    EXPECT_EQ(synthesizeModal(s, pcm), 0u);
    EXPECT_TRUE(pcm.empty());
}

// ---------------------------------------------------------------------------
// PhISEM synthesis
// ---------------------------------------------------------------------------

TEST(Phisem, GrainCountApproximatesRateTimesDuration)
{
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(1e-6f, 1.0f);
    auto sample = [&]() { return dist(rng); };

    const float rate = 1000.0f;
    const float dur = 0.12f;
    const auto onsets = phisemGrainOnsets(rate, dur, kSynthSampleRate, sample);

    const float expected = rate * dur;  // ≈ 120
    EXPECT_GT(onsets.size(), static_cast<std::size_t>(expected * 0.6f));
    EXPECT_LT(onsets.size(), static_cast<std::size_t>(expected * 1.4f));

    // Onsets are strictly within the buffer and ascending.
    const int maxSample = static_cast<int>(dur * kSynthSampleRate);
    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        EXPECT_GE(onsets[i], 0);
        EXPECT_LT(onsets[i], maxSample);
        if (i > 0) EXPECT_GE(onsets[i], onsets[i - 1]);
    }
}

TEST(Phisem, OnsetsAreDeterministicForSameSeed)
{
    auto run = []() {
        std::mt19937 rng(777);
        std::uniform_real_distribution<float> dist(1e-6f, 1.0f);
        auto sample = [&]() { return dist(rng); };
        return phisemGrainOnsets(900.0f, 0.14f, kSynthSampleRate, sample);
    };
    EXPECT_EQ(run(), run()) << "same seed must reproduce the same grain pattern";
}

TEST(Phisem, SynthesizesAudibleSandBurst)
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(1e-6f, 1.0f);
    auto sample = [&]() { return dist(rng); };

    PhisemStrike s;
    s.centreHz = 1800.0f;
    s.qual = 2.0f;
    s.eventRateHz = 1200.0f;
    s.durSec = 0.12f;
    s.energyDecay = 18.0f;
    s.energyGain = 1.0f;

    std::vector<std::int16_t> pcm;
    const std::size_t grains = synthesizePhisem(s, pcm, sample);

    EXPECT_GT(grains, 0u);
    EXPECT_EQ(pcm.size(), static_cast<std::size_t>(0.12f * kSynthSampleRate + 0.5f));
    EXPECT_GT(peakAbs(pcm, 0, pcm.size()), 0) << "an aggregate strike must make sound";
}

TEST(Phisem, ZeroRateProducesNoGrains)
{
    auto sample = []() { return 0.5f; };
    EXPECT_TRUE(phisemGrainOnsets(0.0f, 0.1f, kSynthSampleRate, sample).empty());
}
