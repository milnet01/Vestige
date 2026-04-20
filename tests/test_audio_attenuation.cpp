// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_attenuation.cpp
/// @brief Phase 10 spatial-audio coverage for the distance-attenuation
///        curve module — each canonical form evaluated at known
///        distances, plus mapping to OpenAL's native constants.

#include <gtest/gtest.h>

#include "audio/audio_attenuation.h"

#include <AL/al.h>

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;

AttenuationParams defaultParams()
{
    AttenuationParams p;
    p.referenceDistance = 1.0f;
    p.maxDistance       = 50.0f;
    p.rolloffFactor     = 1.0f;
    return p;
}
}

// -- Labels & AL mapping ---------------------------------------------

TEST(AudioAttenuation, ModelLabelsAreStable)
{
    EXPECT_STREQ(attenuationModelLabel(AttenuationModel::None),            "None");
    EXPECT_STREQ(attenuationModelLabel(AttenuationModel::Linear),          "Linear");
    EXPECT_STREQ(attenuationModelLabel(AttenuationModel::InverseDistance), "InverseDistance");
    EXPECT_STREQ(attenuationModelLabel(AttenuationModel::Exponential),     "Exponential");
}

TEST(AudioAttenuation, ModelMapsToOpenAlConstant)
{
    EXPECT_EQ(alDistanceModelFor(AttenuationModel::None),
              static_cast<int>(AL_NONE));
    EXPECT_EQ(alDistanceModelFor(AttenuationModel::Linear),
              static_cast<int>(AL_LINEAR_DISTANCE_CLAMPED));
    EXPECT_EQ(alDistanceModelFor(AttenuationModel::InverseDistance),
              static_cast<int>(AL_INVERSE_DISTANCE_CLAMPED));
    EXPECT_EQ(alDistanceModelFor(AttenuationModel::Exponential),
              static_cast<int>(AL_EXPONENT_DISTANCE_CLAMPED));
}

// -- Below reference distance holds full gain ------------------------

TEST(AudioAttenuation, GainIsUnityAtOrBelowReferenceDistance)
{
    AttenuationParams p = defaultParams();
    for (AttenuationModel m : {AttenuationModel::Linear,
                                AttenuationModel::InverseDistance,
                                AttenuationModel::Exponential})
    {
        EXPECT_NEAR(computeAttenuation(m, p, 0.0f), 1.0f, kEps)
            << "model=" << attenuationModelLabel(m);
        EXPECT_NEAR(computeAttenuation(m, p, p.referenceDistance), 1.0f, kEps)
            << "model=" << attenuationModelLabel(m);
        EXPECT_NEAR(computeAttenuation(m, p, p.referenceDistance * 0.5f), 1.0f, kEps)
            << "model=" << attenuationModelLabel(m);
    }
}

// -- None model is a pass-through -----------------------------------

TEST(AudioAttenuation, NoneReturnsUnityAtAnyDistance)
{
    AttenuationParams p = defaultParams();
    for (float d : {0.0f, 1.0f, 50.0f, 500.0f, 1e6f})
    {
        EXPECT_NEAR(computeAttenuation(AttenuationModel::None, p, d), 1.0f, kEps)
            << "d=" << d;
    }
}

// -- Linear curve --------------------------------------------------

TEST(AudioAttenuation, LinearHitsZeroAtMaxDistance)
{
    // OpenAL spec: 1 - (d - refDist) / (maxDist - refDist), clamped.
    // Default params land exactly at 0 when d == maxDist.
    AttenuationParams p = defaultParams();
    EXPECT_NEAR(computeAttenuation(AttenuationModel::Linear, p, p.maxDistance),
                0.0f, kEps);
}

TEST(AudioAttenuation, LinearHalfwayPointIsHalfGain)
{
    // d = (refDist + maxDist)/2 should give gain 0.5 with rolloff=1.
    AttenuationParams p = defaultParams();
    const float mid = (p.referenceDistance + p.maxDistance) * 0.5f;
    EXPECT_NEAR(computeAttenuation(AttenuationModel::Linear, p, mid),
                0.5f, kEps);
}

TEST(AudioAttenuation, LinearClampsPastMaxDistance)
{
    // Past maxDistance, the `_CLAMPED` model holds the gain at its
    // maxDistance value (= 0 for linear). Ensures no negative gains.
    AttenuationParams p = defaultParams();
    EXPECT_NEAR(computeAttenuation(AttenuationModel::Linear, p, 1000.0f),
                0.0f, kEps);
}

// -- Inverse-distance curve ---------------------------------------

TEST(AudioAttenuation, InverseDistanceMatchesClassicFormula)
{
    // gain = refDist / (refDist + rolloff * (d - refDist))
    // With refDist=1, rolloff=1, d=2 → 1/(1+1) = 0.5.
    AttenuationParams p = defaultParams();
    EXPECT_NEAR(computeAttenuation(AttenuationModel::InverseDistance, p, 2.0f),
                0.5f, kEps);
    // d=5 → 1/(1+4) = 0.2
    EXPECT_NEAR(computeAttenuation(AttenuationModel::InverseDistance, p, 5.0f),
                0.2f, kEps);
}

TEST(AudioAttenuation, InverseDistanceFallsMonotonically)
{
    AttenuationParams p = defaultParams();
    float prev = 1.0f;
    for (float d = 1.0f; d <= 40.0f; d += 1.0f)
    {
        float g = computeAttenuation(AttenuationModel::InverseDistance, p, d);
        EXPECT_LE(g, prev + kEps) << "non-monotonic at d=" << d;
        prev = g;
    }
}

TEST(AudioAttenuation, InverseDistanceClampsAtMaxDistance)
{
    AttenuationParams p = defaultParams();
    const float atMax = computeAttenuation(AttenuationModel::InverseDistance, p, p.maxDistance);
    const float past  = computeAttenuation(AttenuationModel::InverseDistance, p, 1000.0f);
    EXPECT_NEAR(past, atMax, kEps);
}

// -- Exponential curve --------------------------------------------

TEST(AudioAttenuation, ExponentialMatchesPowerFormula)
{
    // gain = (d/refDist)^(-rolloff). refDist=1, rolloff=1, d=2 → 0.5.
    // rolloff=2, d=2 → 0.25 (classic inverse-square).
    AttenuationParams p = defaultParams();
    EXPECT_NEAR(computeAttenuation(AttenuationModel::Exponential, p, 2.0f),
                0.5f, kEps);

    p.rolloffFactor = 2.0f;
    EXPECT_NEAR(computeAttenuation(AttenuationModel::Exponential, p, 2.0f),
                0.25f, kEps);

    // rolloff=0 flattens the curve entirely.
    p.rolloffFactor = 0.0f;
    EXPECT_NEAR(computeAttenuation(AttenuationModel::Exponential, p, 100.0f),
                1.0f, kEps);
}

TEST(AudioAttenuation, ExponentialClampsAtMaxDistance)
{
    AttenuationParams p = defaultParams();
    p.rolloffFactor = 2.0f;
    const float atMax = computeAttenuation(AttenuationModel::Exponential, p, p.maxDistance);
    const float past  = computeAttenuation(AttenuationModel::Exponential, p, 1000.0f);
    EXPECT_NEAR(past, atMax, kEps);
}

// -- Defensive parameter handling --------------------------------

TEST(AudioAttenuation, NegativeDistanceTreatedAsZero)
{
    AttenuationParams p = defaultParams();
    for (AttenuationModel m : {AttenuationModel::Linear,
                                AttenuationModel::InverseDistance,
                                AttenuationModel::Exponential})
    {
        EXPECT_NEAR(computeAttenuation(m, p, -5.0f), 1.0f, kEps)
            << "model=" << attenuationModelLabel(m);
    }
}

TEST(AudioAttenuation, ZeroSpanLinearReturnsUnity)
{
    // If refDist == maxDist the linear span is zero; avoid division
    // by zero and return full gain.
    AttenuationParams p;
    p.referenceDistance = 5.0f;
    p.maxDistance       = 5.0f;
    EXPECT_NEAR(computeAttenuation(AttenuationModel::Linear, p, 5.0f), 1.0f, kEps);
}

TEST(AudioAttenuation, RolloffZeroFlattensInverseDistance)
{
    // rolloff=0 simplifies inverse-distance to refDist/refDist = 1.
    AttenuationParams p = defaultParams();
    p.rolloffFactor = 0.0f;
    EXPECT_NEAR(computeAttenuation(AttenuationModel::InverseDistance, p, 10.0f),
                1.0f, kEps);
    EXPECT_NEAR(computeAttenuation(AttenuationModel::InverseDistance, p, 50.0f),
                1.0f, kEps);
}
