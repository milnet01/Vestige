// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_material_sound_bank.cpp
/// @brief AX4 S5 — material sound bank: JSON load, curve/jitter-driven
///        synthesis, determinism, and the §5c variation helpers.
#include "audio/procedural/audio_curves.h"
#include "audio/procedural/material_sound_bank.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

using namespace Vestige;
using namespace Vestige::Procedural;

namespace
{

/// A constant 0.5 uniform: makes both jitter helpers a no-op (the ±spread maps
/// to 0), so a test can isolate the FW-curve effect from per-strike randomness.
std::function<float()> halfSampler()
{
    return []() { return 0.5f; };
}

/// A seeded mt19937 sampler — deterministic across two equally-seeded calls.
std::function<float()> seededSampler(std::uint32_t seed)
{
    auto rng = std::make_shared<std::mt19937>(seed);
    auto dist = std::make_shared<std::uniform_real_distribution<float>>(1e-6f, 1.0f);
    return [rng, dist]() { return (*dist)(*rng); };
}

int peakAbs(const std::vector<std::int16_t>& pcm)
{
    int peak = 0;
    for (std::int16_t s : pcm) peak = std::max(peak, std::abs(static_cast<int>(s)));
    return peak;
}

constexpr char kBankJson[] = R"({
  "version": 1,
  "materials": {
    "stone": { "model": "modal", "modes": [ {"f":380,"d":34,"g":1.0}, {"f":920,"d":48,"g":0.6} ],
               "durSec": 0.18, "pitchJitterCents": 40, "gainJitterDb": 2.0 },
    "sand":  { "model": "phisem", "centreHz": 1800, "qual": 2.0, "eventRateHz": 1200,
               "durSec": 0.12, "energyDecay": 18 },
    "bogus_material": { "model": "modal", "modes": [ {"f":100,"d":10,"g":1.0} ], "durSec": 0.1 }
  }
})";

std::size_t expectedSamples(float durSec)
{
    return static_cast<std::size_t>(durSec * kSynthSampleRate + 0.5f);
}

}  // namespace

// --- Built-in fallback -----------------------------------------------------

TEST(MaterialSoundBank, FreshBankSynthesizesFallbackThudForEveryMaterial)
{
    MaterialSoundBank bank;  // nothing loaded
    std::vector<std::int16_t> pcm;
    // The §6 default is a single-mode 0.12 s modal thud.
    const std::size_t n = bank.synthesize(SurfaceMaterial::Metal, 4.0f, 1.0f, halfSampler(), pcm);
    EXPECT_GT(n, 0u);
    EXPECT_EQ(pcm.size(), expectedSamples(0.12f));
    EXPECT_GT(peakAbs(pcm), 0) << "the fallback thud must make sound";
}

// --- JSON load -------------------------------------------------------------

TEST(MaterialSoundBank, LoadsModalEntryAndSynthesizesAtBankDuration)
{
    MaterialSoundBank bank;
    ASSERT_TRUE(bank.loadFromString(kBankJson));

    std::vector<std::int16_t> pcm;
    bank.synthesize(SurfaceMaterial::Stone, 3.0f, 1.0f, halfSampler(), pcm);
    EXPECT_EQ(pcm.size(), expectedSamples(0.18f)) << "stone durSec from the bank";
    EXPECT_GT(peakAbs(pcm), 0);
}

TEST(MaterialSoundBank, LoadsPhisemEntryAndProducesGrains)
{
    MaterialSoundBank bank;
    ASSERT_TRUE(bank.loadFromString(kBankJson));

    std::vector<std::int16_t> pcm;
    const std::size_t grains =
        bank.synthesize(SurfaceMaterial::Sand, 2.0f, 1.0f, seededSampler(7), pcm);
    EXPECT_GT(grains, 0u);
    EXPECT_EQ(pcm.size(), expectedSamples(0.12f));
    EXPECT_GT(peakAbs(pcm), 0);
}

TEST(MaterialSoundBank, UnknownMaterialNameIsIgnoredButLoadStillSucceeds)
{
    MaterialSoundBank bank;
    EXPECT_TRUE(bank.loadFromString(kBankJson));  // "bogus_material" silently skipped
    // Stone (a valid sibling key) still parsed.
    std::vector<std::int16_t> pcm;
    bank.synthesize(SurfaceMaterial::Stone, 3.0f, 1.0f, halfSampler(), pcm);
    EXPECT_EQ(pcm.size(), expectedSamples(0.18f));
}

TEST(MaterialSoundBank, MalformedJsonLeavesBankUnchanged)
{
    MaterialSoundBank bank;
    EXPECT_FALSE(bank.loadFromString("{ this is not valid json"));
    EXPECT_FALSE(bank.loadFromString("[1, 2, 3]"));  // valid JSON, wrong shape
    // Fallback thud still works.
    std::vector<std::int16_t> pcm;
    EXPECT_GT(bank.synthesize(SurfaceMaterial::Stone, 4.0f, 1.0f, halfSampler(), pcm), 0u);
    EXPECT_EQ(pcm.size(), expectedSamples(0.12f)) << "still the built-in thud";
}

// --- Curve application -----------------------------------------------------

TEST(MaterialSoundBank, HarderStrikeIsLouder)
{
    MaterialSoundBank bank;
    ASSERT_TRUE(bank.loadFromString(kBankJson));

    std::vector<std::int16_t> soft, loud;
    // halfSampler() neutralises jitter, so the only difference is the FW
    // loudness curve, which rises with approach speed.
    bank.synthesize(SurfaceMaterial::Stone, 1.0f, 1.0f, halfSampler(), soft);
    bank.synthesize(SurfaceMaterial::Stone, 8.0f, 1.0f, halfSampler(), loud);
    EXPECT_LT(peakAbs(soft), peakAbs(loud));
}

TEST(MaterialSoundBank, EnvelopeScaleLengthensTheRingUpToTheCap)
{
    MaterialSoundBank bank;
    ASSERT_TRUE(bank.loadFromString(kBankJson));

    std::vector<std::int16_t> footstep, impact;
    bank.synthesize(SurfaceMaterial::Stone, 4.0f, 1.0f, halfSampler(), footstep);
    bank.synthesize(SurfaceMaterial::Stone, 4.0f, 1.5f, halfSampler(), impact);
    EXPECT_EQ(footstep.size(), expectedSamples(0.18f));
    EXPECT_EQ(impact.size(), expectedSamples(0.18f * 1.5f));  // 0.27 s < 0.35 cap
    EXPECT_GT(impact.size(), footstep.size());
}

TEST(MaterialSoundBank, FasterAggregateContactSpawnsMoreGrains)
{
    MaterialSoundBank bank;
    ASSERT_TRUE(bank.loadFromString(kBankJson));

    // Same seed → identical uniform sequence, so grain count is monotonic in
    // the FW-curve-driven Poisson rate (higher speed → higher rate).
    std::vector<std::int16_t> slowPcm, fastPcm;
    const std::size_t slow = bank.synthesize(SurfaceMaterial::Sand, 0.5f, 1.0f, seededSampler(99), slowPcm);
    const std::size_t fast = bank.synthesize(SurfaceMaterial::Sand, 8.0f, 1.0f, seededSampler(99), fastPcm);
    EXPECT_GT(fast, slow);
}

// --- Determinism -----------------------------------------------------------

TEST(MaterialSoundBank, SameSeedReproducesIdenticalPcm)
{
    MaterialSoundBank bank;
    ASSERT_TRUE(bank.loadFromString(kBankJson));

    std::vector<std::int16_t> a, b;
    bank.synthesize(SurfaceMaterial::Stone, 5.0f, 1.0f, seededSampler(2024), a);
    bank.synthesize(SurfaceMaterial::Stone, 5.0f, 1.0f, seededSampler(2024), b);
    EXPECT_EQ(a, b) << "a seeded emitter must re-synthesise byte-identically";
}

// --- Variation helpers (§5c) ----------------------------------------------

TEST(VariationHelpers, JitterPitchIsNeutralAtMidpointAndSpansSpread)
{
    auto mid = []() { return 0.5f; };
    auto lo  = []() { return 0.0f; };
    auto hi  = []() { return 1.0f - 1e-7f; };

    EXPECT_NEAR(jitterPitch(440.0f, 50.0f, mid), 440.0f, 1e-3f);   // ±0 cents
    // +50 cents ≈ ×2^(50/1200); −50 cents ≈ ×2^(−50/1200).
    EXPECT_NEAR(jitterPitch(440.0f, 50.0f, hi), 440.0f * std::pow(2.0f, 50.0f / 1200.0f), 1e-2f);
    EXPECT_NEAR(jitterPitch(440.0f, 50.0f, lo), 440.0f * std::pow(2.0f, -50.0f / 1200.0f), 1e-2f);
}

TEST(VariationHelpers, JitterGainIsNeutralAtMidpointAndSpansSpread)
{
    auto mid = []() { return 0.5f; };
    auto hi  = []() { return 1.0f - 1e-7f; };

    EXPECT_NEAR(jitterGain(1.0f, 6.0f, mid), 1.0f, 1e-4f);          // ±0 dB
    EXPECT_NEAR(jitterGain(1.0f, 6.0f, hi), std::pow(10.0f, 6.0f / 20.0f), 1e-3f);  // +6 dB ≈ ×2
}
