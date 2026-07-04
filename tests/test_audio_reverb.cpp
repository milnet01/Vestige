// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_reverb.cpp
/// @brief Phase 10 spatial-audio coverage for reverb presets + zone
///        weight falloff + param blending for smooth transitions.

#include <gtest/gtest.h>

#include "audio/audio_engine.h"
#include "audio/audio_reverb.h"
#include "audio/reverb_ir_pool.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;
}

// -- Preset labels -------------------------------------------------

TEST(AudioReverb, PresetLabelsAreStable)
{
    EXPECT_STREQ(reverbPresetLabel(ReverbPreset::Generic),    "Generic");
    EXPECT_STREQ(reverbPresetLabel(ReverbPreset::SmallRoom),  "SmallRoom");
    EXPECT_STREQ(reverbPresetLabel(ReverbPreset::LargeHall),  "LargeHall");
    EXPECT_STREQ(reverbPresetLabel(ReverbPreset::Cave),       "Cave");
    EXPECT_STREQ(reverbPresetLabel(ReverbPreset::Outdoor),    "Outdoor");
    EXPECT_STREQ(reverbPresetLabel(ReverbPreset::Underwater), "Underwater");
}

// -- Preset sanity --------------------------------------------------

TEST(AudioReverb, PresetsAreInValidEfxRanges)
{
    for (auto preset : {ReverbPreset::Generic,
                         ReverbPreset::SmallRoom,
                         ReverbPreset::LargeHall,
                         ReverbPreset::Cave,
                         ReverbPreset::Outdoor,
                         ReverbPreset::Underwater})
    {
        auto p = reverbPresetParams(preset);
        EXPECT_GE(p.decayTime,        0.1f);
        EXPECT_LE(p.decayTime,        20.0f);
        EXPECT_GE(p.density,          0.0f);
        EXPECT_LE(p.density,          1.0f);
        EXPECT_GE(p.diffusion,        0.0f);
        EXPECT_LE(p.diffusion,        1.0f);
        EXPECT_GE(p.gain,             0.0f);
        EXPECT_LE(p.gain,             1.0f);
        EXPECT_GE(p.gainHf,           0.0f);
        EXPECT_LE(p.gainHf,           1.0f);
        EXPECT_GE(p.reflectionsDelay, 0.0f);
        EXPECT_GE(p.lateReverbDelay,  0.0f);
    }
}

TEST(AudioReverb, SmallRoomHasShortestDecay)
{
    // Invariant: SmallRoom has the shortest decay of any preset, so
    // setting a preset map incorrectly (e.g. swapping Hall and Room)
    // will break this test.
    const auto room = reverbPresetParams(ReverbPreset::SmallRoom);
    for (auto preset : {ReverbPreset::Generic,
                         ReverbPreset::LargeHall,
                         ReverbPreset::Cave,
                         ReverbPreset::Outdoor,
                         ReverbPreset::Underwater})
    {
        EXPECT_LE(room.decayTime,
                  reverbPresetParams(preset).decayTime + kEps)
            << "SmallRoom expected shorter decay than "
            << reverbPresetLabel(preset);
    }
}

TEST(AudioReverb, CaveHasLongestDecay)
{
    const auto cave = reverbPresetParams(ReverbPreset::Cave);
    for (auto preset : {ReverbPreset::Generic,
                         ReverbPreset::SmallRoom,
                         ReverbPreset::LargeHall,
                         ReverbPreset::Outdoor,
                         ReverbPreset::Underwater})
    {
        EXPECT_GE(cave.decayTime,
                  reverbPresetParams(preset).decayTime - kEps)
            << "Cave expected longest decay vs "
            << reverbPresetLabel(preset);
    }
}

TEST(AudioReverb, UnderwaterStronglyDampsHighFrequencies)
{
    // Underwater preset should have the lowest gainHf across the set
    // — muffled reading for submerged-scene playback.
    const auto water = reverbPresetParams(ReverbPreset::Underwater);
    for (auto preset : {ReverbPreset::Generic,
                         ReverbPreset::SmallRoom,
                         ReverbPreset::LargeHall,
                         ReverbPreset::Cave,
                         ReverbPreset::Outdoor})
    {
        EXPECT_LE(water.gainHf,
                  reverbPresetParams(preset).gainHf + kEps)
            << "Underwater expected strongest HF damping vs "
            << reverbPresetLabel(preset);
    }
}

// -- Reverb zone weight falloff ------------------------------------

TEST(AudioReverb, InsideCoreWeightIsFull)
{
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 5.0f, 0.0f),  1.0f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 5.0f, 5.0f),  1.0f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 5.0f, 10.0f), 1.0f, kEps);
}

TEST(AudioReverb, ZeroFalloffIsHardStep)
{
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 0.0f, 10.0f), 1.0f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 0.0f, 10.001f), 0.0f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 0.0f, 20.0f), 0.0f, kEps);
}

TEST(AudioReverb, FalloffIsLinear)
{
    // radius=10, band=4 → midway through band (d=12) gives weight 0.5
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 4.0f, 10.0f), 1.0f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 4.0f, 11.0f), 0.75f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 4.0f, 12.0f), 0.50f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 4.0f, 13.0f), 0.25f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 4.0f, 14.0f), 0.00f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 4.0f, 50.0f), 0.00f, kEps);
}

TEST(AudioReverb, NegativeInputsClampToZero)
{
    // Negative distance treated as 0 → inside core.
    EXPECT_NEAR(computeReverbZoneWeight(10.0f, 4.0f, -5.0f), 1.0f, kEps);
    // Negative radius treated as 0 → distance 0 is inside, anything
    // beyond the falloff band is zero.
    EXPECT_NEAR(computeReverbZoneWeight(-1.0f, 4.0f, 0.0f), 1.0f, kEps);
    EXPECT_NEAR(computeReverbZoneWeight(-1.0f, 4.0f, 5.0f), 0.0f, kEps);
}

// -- blendReverbParams --------------------------------------------

TEST(AudioReverb, BlendAtZeroReturnsA)
{
    const auto a = reverbPresetParams(ReverbPreset::SmallRoom);
    const auto b = reverbPresetParams(ReverbPreset::Cave);
    const auto blended = blendReverbParams(a, b, 0.0f);
    EXPECT_EQ(blended, a);
}

TEST(AudioReverb, BlendAtOneReturnsB)
{
    const auto a = reverbPresetParams(ReverbPreset::SmallRoom);
    const auto b = reverbPresetParams(ReverbPreset::Cave);
    const auto blended = blendReverbParams(a, b, 1.0f);
    EXPECT_EQ(blended, b);
}

TEST(AudioReverb, BlendAtHalfIsMidpoint)
{
    ReverbParams a;
    a.decayTime = 1.0f;
    a.gain      = 0.2f;

    ReverbParams b;
    b.decayTime = 5.0f;
    b.gain      = 0.8f;

    const auto mid = blendReverbParams(a, b, 0.5f);
    EXPECT_NEAR(mid.decayTime, 3.0f, kEps);
    EXPECT_NEAR(mid.gain,      0.5f, kEps);
}

TEST(AudioReverb, BlendClampsOutOfRangeT)
{
    const auto a = reverbPresetParams(ReverbPreset::SmallRoom);
    const auto b = reverbPresetParams(ReverbPreset::Cave);
    EXPECT_EQ(blendReverbParams(a, b, -0.5f), a);
    EXPECT_EQ(blendReverbParams(a, b,  1.5f), b);
}

// -- AX2 R1: AudioEngine reverb accessors (headless, never initialised) ----
//
// With no audio device the EFX aux slot is never created, so isReverbAvailable
// is false and the AL-object setters no-op — but the wet-gain value is still
// stored + clamped so a getter reflects the request (and R3/R4 can drive it).

TEST(AudioReverb, EngineIsDryAndReverbUnavailableWhenUninitialised)
{
    AudioEngine engine;  // never initialised — no audio device / EFX slot
    EXPECT_FALSE(engine.isReverbAvailable());
    EXPECT_NEAR(engine.reverbWetGain(), 0.0f, kEps);
}

TEST(AudioReverb, SetReverbWetGainStoresAndClampsToUnitRange)
{
    AudioEngine engine;
    engine.setReverbWetGain(0.4f);
    EXPECT_NEAR(engine.reverbWetGain(), 0.4f, kEps);
    engine.setReverbWetGain(1.5f);
    EXPECT_NEAR(engine.reverbWetGain(), 1.0f, kEps);
    engine.setReverbWetGain(-0.3f);
    EXPECT_NEAR(engine.reverbWetGain(), 0.0f, kEps);
}

// -- AX2 R4: reverb settings storage (read by ReverbSystem / init) ---------

TEST(AudioReverb, ReverbSettingsDefaultsMatchDesign)
{
    AudioEngine engine;
    EXPECT_TRUE(engine.isReverbEnabled());              // master on
    EXPECT_NEAR(engine.reverbWetCap(), 0.5f, kEps);     // accessibility cap
    EXPECT_TRUE(engine.isReverbConvolutionAllowed());   // convolution allowed
}

TEST(AudioReverb, ReverbSettingsSettersStoreVerbatim)
{
    AudioEngine engine;
    engine.setReverbEnabled(false);
    engine.setReverbWetCap(0.25f);
    engine.setReverbConvolutionAllowed(false);
    EXPECT_FALSE(engine.isReverbEnabled());
    EXPECT_NEAR(engine.reverbWetCap(), 0.25f, kEps);
    EXPECT_FALSE(engine.isReverbConvolutionAllowed());
}

TEST(AudioReverb, SetReverbParamsIsANoOpWithoutASlot)
{
    // No slot/effect exist headless, so this must not crash or change
    // availability — it simply has nowhere to push the params.
    AudioEngine engine;
    engine.setReverbParams(reverbPresetParams(ReverbPreset::Cave));
    EXPECT_FALSE(engine.isReverbAvailable());
}

// -- AX2 R2: IR loading + convolution backend (headless contract) ----------
//
// Backend selection needs a live device + the experimental extension, neither
// present headless — so an uninitialised engine reports the parametric backend
// (the R1 fallback) and the IR-load path degrades gracefully to a no-op. The
// LRU / pin / byte-limit logic itself is covered device-free in
// test_reverb_ir_pool.cpp.

TEST(AudioReverb, BackendDefaultsToParametricWhenUninitialised)
{
    AudioEngine engine;  // no device → the R1 parametric fallback stands
    EXPECT_EQ(engine.reverbBackend(), AudioEngine::ReverbBackend::Parametric);
}

TEST(AudioReverb, LoadReverbIrReturnsZeroAndAddsNothingWhenUnavailable)
{
    AudioEngine engine;  // no device — decode/alGenBuffers never run
    EXPECT_EQ(engine.loadReverbIr("assets/audio/ir/any.wav"), 0u);
    EXPECT_EQ(engine.reverbIrCount(), 0u);
    EXPECT_EQ(engine.reverbIrPoolBytes(), 0u);
    EXPECT_EQ(engine.attachedReverbIr(), 0u);
}

TEST(AudioReverb, AttachReverbIrIsANoOpOnParametricBackend)
{
    // Parametric backend (and no slot) headless: attach must not pin anything.
    AudioEngine engine;
    engine.attachReverbIr(42u);
    EXPECT_EQ(engine.attachedReverbIr(), 0u);
}

TEST(AudioReverb, ReverbIrPoolLimitDefaultsTo64MiB)
{
    AudioEngine engine;
    EXPECT_EQ(engine.reverbIrPoolLimitBytes(), ReverbIrPool::kDefaultLimitBytes);
}
