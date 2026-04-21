// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_doppler.cpp
/// @brief Phase 10 spatial-audio coverage for the Doppler-shift
///        module — verifies the sign conventions, the speed-of-sound
///        clamp, and the pass-through cases (zero velocity, disabled
///        factor, co-located endpoints).

#include <gtest/gtest.h>

#include "audio/audio_doppler.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;
constexpr float kSpeedOfSound = 343.3f;

DopplerParams defaultParams()
{
    DopplerParams p;
    p.speedOfSound = kSpeedOfSound;
    p.dopplerFactor = 1.0f;
    return p;
}

// Reference oracle — recomputes the ratio with the same formula the
// implementation uses, so tests cross-check expected magnitudes
// instead of hard-coding floats for every scenario.
float expectedRatio(float vLs, float vSs,
                    float dopplerFactor = 1.0f,
                    float speedOfSound  = kSpeedOfSound)
{
    return (speedOfSound - dopplerFactor * vLs) /
           (speedOfSound - dopplerFactor * vSs);
}
}

// -- Zero-velocity / no-op cases ------------------------------------

TEST(AudioDoppler, ZeroVelocitiesReturnUnity)
{
    DopplerParams p = defaultParams();
    EXPECT_NEAR(computeDopplerPitchRatio(p, {0, 0, 0}, {0, 0, 0},
                                            {10, 0, 0}, {0, 0, 0}),
                1.0f, kEps);
}

TEST(AudioDoppler, CoLocatedSourceListenerReturnsUnity)
{
    DopplerParams p = defaultParams();
    // Any velocity values — without a defined axis the effect collapses.
    EXPECT_NEAR(computeDopplerPitchRatio(p,
                                          {5, 5, 5}, {30, 0, 0},
                                          {5, 5, 5}, {10, 0, 0}),
                1.0f, kEps);
}

TEST(AudioDoppler, DopplerFactorZeroDisablesEffect)
{
    DopplerParams p = defaultParams();
    p.dopplerFactor = 0.0f;
    // Even for supersonic approach the ratio must stay at 1.
    EXPECT_NEAR(computeDopplerPitchRatio(p,
                                          {0, 0, 0}, { 200.0f, 0, 0},
                                          {10, 0, 0}, {-200.0f, 0, 0}),
                1.0f, kEps);
}

TEST(AudioDoppler, NegativeDopplerFactorTreatedAsDisabled)
{
    DopplerParams p = defaultParams();
    p.dopplerFactor = -1.0f;
    EXPECT_NEAR(computeDopplerPitchRatio(p,
                                          {0, 0, 0}, {30, 0, 0},
                                          {5, 0, 0}, {0, 0, 0}),
                1.0f, kEps);
}

TEST(AudioDoppler, NonPositiveSpeedOfSoundReturnsUnity)
{
    DopplerParams p = defaultParams();
    p.speedOfSound = 0.0f;
    EXPECT_NEAR(computeDopplerPitchRatio(p,
                                          {0, 0, 0}, {30, 0, 0},
                                          {5, 0, 0}, {0, 0, 0}),
                1.0f, kEps);
}

// -- Source motion along the source→listener axis ------------------

TEST(AudioDoppler, SourceApproachingListenerRaisesPitch)
{
    // Source at origin, listener on +X. Source velocity +X → toward
    // listener → vSs positive → denominator shrinks → ratio > 1.
    DopplerParams p = defaultParams();
    const glm::vec3 srcPos(0.0f), lisPos(10.0f, 0.0f, 0.0f);
    const glm::vec3 srcVel(30.0f, 0.0f, 0.0f);
    const float ratio = computeDopplerPitchRatio(p, srcPos, srcVel,
                                                   lisPos, {0, 0, 0});
    EXPECT_GT(ratio, 1.0f);
    EXPECT_NEAR(ratio, expectedRatio(0.0f, 30.0f), kEps);
}

TEST(AudioDoppler, SourceRecedingFromListenerLowersPitch)
{
    // Source velocity -X → away from listener → vSs negative → ratio < 1.
    DopplerParams p = defaultParams();
    const float ratio = computeDopplerPitchRatio(p,
                                                   {0, 0, 0}, {-30.0f, 0, 0},
                                                   {10, 0, 0}, {0, 0, 0});
    EXPECT_LT(ratio, 1.0f);
    EXPECT_NEAR(ratio, expectedRatio(0.0f, -30.0f), kEps);
}

// -- Listener motion along the source→listener axis ----------------

TEST(AudioDoppler, ListenerApproachingSourceRaisesPitch)
{
    // Listener at +10X with velocity -X → toward source → vLs < 0 →
    // numerator grows → ratio > 1.
    DopplerParams p = defaultParams();
    const float ratio = computeDopplerPitchRatio(p,
                                                   {0, 0, 0}, {0, 0, 0},
                                                   {10, 0, 0}, {-30.0f, 0, 0});
    EXPECT_GT(ratio, 1.0f);
    EXPECT_NEAR(ratio, expectedRatio(-30.0f, 0.0f), kEps);
}

TEST(AudioDoppler, ListenerRecedingFromSourceLowersPitch)
{
    DopplerParams p = defaultParams();
    const float ratio = computeDopplerPitchRatio(p,
                                                   {0, 0, 0}, {0, 0, 0},
                                                   {10, 0, 0}, {30.0f, 0, 0});
    EXPECT_LT(ratio, 1.0f);
    EXPECT_NEAR(ratio, expectedRatio(30.0f, 0.0f), kEps);
}

// -- Perpendicular motion → no Doppler effect ----------------------

TEST(AudioDoppler, PerpendicularMotionProducesNoShift)
{
    // Source and listener on the X axis; velocities purely along Y.
    // Projections onto the source→listener axis (+X) are zero.
    DopplerParams p = defaultParams();
    const float ratio = computeDopplerPitchRatio(p,
                                                   {0, 0, 0}, {0, 50.0f, 0},
                                                   {10, 0, 0}, {0, 25.0f, 0});
    EXPECT_NEAR(ratio, 1.0f, kEps);
}

// -- Combined motion ----------------------------------------------

TEST(AudioDoppler, BothApproachingRaisesPitchMoreThanEither)
{
    // Listener moving toward source AND source moving toward listener.
    // Expected ratio = (SS + 30) / (SS - 30) — both sides amplify.
    DopplerParams p = defaultParams();
    const float ratio = computeDopplerPitchRatio(p,
                                                   {0, 0, 0}, { 30.0f, 0, 0},
                                                   {10, 0, 0}, {-30.0f, 0, 0});
    EXPECT_NEAR(ratio, expectedRatio(-30.0f, 30.0f), kEps);
    EXPECT_GT(ratio, 1.0f);

    // Sanity: larger shift than either endpoint alone.
    const float listenerOnly = computeDopplerPitchRatio(p,
                                                          {0, 0, 0}, {0, 0, 0},
                                                          {10, 0, 0}, {-30.0f, 0, 0});
    const float sourceOnly   = computeDopplerPitchRatio(p,
                                                          {0, 0, 0}, {30.0f, 0, 0},
                                                          {10, 0, 0}, {0, 0, 0});
    EXPECT_GT(ratio, listenerOnly);
    EXPECT_GT(ratio, sourceOnly);
}

// -- dopplerFactor scaling -----------------------------------------

TEST(AudioDoppler, DopplerFactorScalesEffect)
{
    DopplerParams p = defaultParams();
    const glm::vec3 srcPos(0, 0, 0);
    const glm::vec3 lisPos(10, 0, 0);
    const glm::vec3 srcVel(30.0f, 0, 0);
    const glm::vec3 lisVel(0, 0, 0);

    const float ratio1 = computeDopplerPitchRatio(p, srcPos, srcVel, lisPos, lisVel);

    p.dopplerFactor = 2.0f;
    const float ratio2 = computeDopplerPitchRatio(p, srcPos, srcVel, lisPos, lisVel);

    // Doubling the factor must move the ratio further from 1.
    EXPECT_GT(ratio2, ratio1);
    EXPECT_NEAR(ratio2, expectedRatio(0.0f, 30.0f, 2.0f), kEps);
}

// -- Speed-of-sound clamp -----------------------------------------

TEST(AudioDoppler, VelocityBeyondSpeedOfSoundIsClamped)
{
    // Source moving toward listener at 2× speed of sound. Clamp pins
    // vSs to +SS, giving denominator SS - SS = 0 → implementation
    // re-guards with epsilon. The expected behaviour is that the
    // ratio saturates rather than diverging to infinity.
    DopplerParams p = defaultParams();
    const float ratio = computeDopplerPitchRatio(p,
                                                   {0, 0, 0},
                                                   {2.0f * kSpeedOfSound, 0, 0},
                                                   {10, 0, 0}, {0, 0, 0});
    // Finite, positive, and strictly greater than any non-clamped case.
    EXPECT_GT(ratio, 1.0f);
    EXPECT_TRUE(std::isfinite(ratio));
}

TEST(AudioDoppler, NegativeVelocityBeyondSpeedOfSoundIsClamped)
{
    // Source receding faster than speed of sound. Clamp pins vSs to
    // -SS → denominator SS + SS = 2·SS. Result should be finite and
    // strictly less than 1 (receding), not go negative.
    DopplerParams p = defaultParams();
    const float ratio = computeDopplerPitchRatio(p,
                                                   {0, 0, 0},
                                                   {-2.0f * kSpeedOfSound, 0, 0},
                                                   {10, 0, 0}, {0, 0, 0});
    EXPECT_GT(ratio, 0.0f);
    EXPECT_LT(ratio, 1.0f);
    EXPECT_TRUE(std::isfinite(ratio));
}

// -- Defaults match OpenAL 1.1 spec defaults -----------------------

TEST(AudioDoppler, DefaultParamsMatchSpecDefaults)
{
    DopplerParams p;  // default-constructed
    EXPECT_NEAR(p.speedOfSound, 343.3f, kEps);
    EXPECT_NEAR(p.dopplerFactor, 1.0f, kEps);
}
