// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_occlusion.cpp
/// @brief Phase 10 spatial-audio coverage for the material-based
///        occlusion / obstruction model.

#include <gtest/gtest.h>

#include "audio/audio_occlusion.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;
}

// -- Material labels ------------------------------------------------

TEST(AudioOcclusion, MaterialLabelsAreStable)
{
    EXPECT_STREQ(occlusionMaterialLabel(AudioOcclusionMaterialPreset::Air),      "Air");
    EXPECT_STREQ(occlusionMaterialLabel(AudioOcclusionMaterialPreset::Cloth),    "Cloth");
    EXPECT_STREQ(occlusionMaterialLabel(AudioOcclusionMaterialPreset::Wood),     "Wood");
    EXPECT_STREQ(occlusionMaterialLabel(AudioOcclusionMaterialPreset::Glass),    "Glass");
    EXPECT_STREQ(occlusionMaterialLabel(AudioOcclusionMaterialPreset::Stone),    "Stone");
    EXPECT_STREQ(occlusionMaterialLabel(AudioOcclusionMaterialPreset::Concrete), "Concrete");
    EXPECT_STREQ(occlusionMaterialLabel(AudioOcclusionMaterialPreset::Metal),    "Metal");
    EXPECT_STREQ(occlusionMaterialLabel(AudioOcclusionMaterialPreset::Water),    "Water");
}

// -- Material presets are in sane ranges + ordered correctly -------

TEST(AudioOcclusion, AirIsFullyTransparent)
{
    auto air = occlusionMaterialFor(AudioOcclusionMaterialPreset::Air);
    EXPECT_NEAR(air.transmissionCoefficient, 1.0f, kEps);
    EXPECT_NEAR(air.lowPassAmount,           0.0f, kEps);
}

TEST(AudioOcclusion, ConcreteIsLeastTransmissive)
{
    // Sanity: concrete should block sound more than any other solid.
    auto concrete = occlusionMaterialFor(AudioOcclusionMaterialPreset::Concrete);
    for (auto preset : {AudioOcclusionMaterialPreset::Cloth,
                         AudioOcclusionMaterialPreset::Wood,
                         AudioOcclusionMaterialPreset::Glass,
                         AudioOcclusionMaterialPreset::Stone,
                         AudioOcclusionMaterialPreset::Metal,
                         AudioOcclusionMaterialPreset::Water})
    {
        EXPECT_LE(concrete.transmissionCoefficient,
                  occlusionMaterialFor(preset).transmissionCoefficient + kEps)
            << "concrete expected less-transmissive than "
            << occlusionMaterialLabel(preset);
    }
}

TEST(AudioOcclusion, ClothIsLeastMuffling)
{
    // Cloth / curtain — soft, thin — should have the lowest low-pass
    // amount among the non-air materials so voices through a tapestry
    // still read as intelligible.
    auto cloth = occlusionMaterialFor(AudioOcclusionMaterialPreset::Cloth);
    for (auto preset : {AudioOcclusionMaterialPreset::Wood,
                         AudioOcclusionMaterialPreset::Stone,
                         AudioOcclusionMaterialPreset::Concrete,
                         AudioOcclusionMaterialPreset::Metal,
                         AudioOcclusionMaterialPreset::Water})
    {
        EXPECT_LE(cloth.lowPassAmount,
                  occlusionMaterialFor(preset).lowPassAmount + kEps)
            << "cloth expected less-muffling than "
            << occlusionMaterialLabel(preset);
    }
}

TEST(AudioOcclusion, AllPresetsStayInValidRange)
{
    for (auto preset : {AudioOcclusionMaterialPreset::Air,
                         AudioOcclusionMaterialPreset::Cloth,
                         AudioOcclusionMaterialPreset::Wood,
                         AudioOcclusionMaterialPreset::Glass,
                         AudioOcclusionMaterialPreset::Stone,
                         AudioOcclusionMaterialPreset::Concrete,
                         AudioOcclusionMaterialPreset::Metal,
                         AudioOcclusionMaterialPreset::Water})
    {
        auto m = occlusionMaterialFor(preset);
        EXPECT_GE(m.transmissionCoefficient, 0.0f);
        EXPECT_LE(m.transmissionCoefficient, 1.0f);
        EXPECT_GE(m.lowPassAmount, 0.0f);
        EXPECT_LE(m.lowPassAmount, 1.0f);
    }
}

// -- computeObstructionGain ---------------------------------------

TEST(AudioObstruction, ZeroFractionKeepsOpenGain)
{
    EXPECT_NEAR(computeObstructionGain(1.0f, 0.2f, 0.0f), 1.0f, kEps);
    EXPECT_NEAR(computeObstructionGain(0.5f, 0.2f, 0.0f), 0.5f, kEps);
}

TEST(AudioObstruction, FullFractionApproachesTransmissionGain)
{
    // Fully blocked by a material with transmission 0.5 → gain
    // drops to openGain * 0.5.
    EXPECT_NEAR(computeObstructionGain(1.0f, 0.5f, 1.0f), 0.5f, kEps);
    EXPECT_NEAR(computeObstructionGain(0.8f, 0.25f, 1.0f), 0.2f, kEps);
}

TEST(AudioObstruction, HalfFractionIsLinearBlend)
{
    // gain = openGain * (1 - 0.5 * (1 - t)) = openGain * (0.5 + 0.5t)
    // openGain=1.0, t=0.4, f=0.5 → 1.0 * (0.5 + 0.2) = 0.7
    EXPECT_NEAR(computeObstructionGain(1.0f, 0.4f, 0.5f), 0.7f, kEps);
}

TEST(AudioObstruction, FractionOutsideRangeIsClamped)
{
    // Negative fraction clamped to 0 → full open gain.
    EXPECT_NEAR(computeObstructionGain(1.0f, 0.2f, -0.5f), 1.0f, kEps);
    // Over-1 fraction clamped to 1 → full material transmission.
    EXPECT_NEAR(computeObstructionGain(1.0f, 0.2f,  2.0f), 0.2f, kEps);
}

TEST(AudioObstruction, TransmissionOutsideRangeIsClamped)
{
    // Negative transmission → 0 (silent on full block).
    EXPECT_NEAR(computeObstructionGain(1.0f, -0.5f, 1.0f), 0.0f, kEps);
    // Over-1 transmission → 1 (no attenuation even on full block).
    EXPECT_NEAR(computeObstructionGain(1.0f,  2.0f, 1.0f), 1.0f, kEps);
}

TEST(AudioObstruction, AirMaterialHasNoEffect)
{
    auto air = occlusionMaterialFor(AudioOcclusionMaterialPreset::Air);
    EXPECT_NEAR(computeObstructionGain(0.8f, air.transmissionCoefficient, 1.0f),
                0.8f, kEps);
}

// -- computeObstructionLowPass ------------------------------------

TEST(AudioObstruction, LowPassZeroFractionIsZero)
{
    EXPECT_NEAR(computeObstructionLowPass(1.0f, 0.0f), 0.0f, kEps);
    EXPECT_NEAR(computeObstructionLowPass(0.5f, 0.0f), 0.0f, kEps);
}

TEST(AudioObstruction, LowPassFullFractionHitsMaterialMax)
{
    EXPECT_NEAR(computeObstructionLowPass(0.8f, 1.0f), 0.8f, kEps);
}

TEST(AudioObstruction, LowPassScalesLinearlyWithFraction)
{
    // amount=1.0, fraction=0.25 → 0.25; fraction=0.75 → 0.75
    EXPECT_NEAR(computeObstructionLowPass(1.0f, 0.25f), 0.25f, kEps);
    EXPECT_NEAR(computeObstructionLowPass(1.0f, 0.75f), 0.75f, kEps);
}

TEST(AudioObstruction, LowPassClampsNegativeAndOverrange)
{
    EXPECT_NEAR(computeObstructionLowPass(-0.5f, 1.0f), 0.0f, kEps);
    EXPECT_NEAR(computeObstructionLowPass( 2.0f, 1.0f), 1.0f, kEps);
    EXPECT_NEAR(computeObstructionLowPass( 0.5f,-1.0f), 0.0f, kEps);
    EXPECT_NEAR(computeObstructionLowPass( 0.5f, 2.0f), 0.5f, kEps);
}
