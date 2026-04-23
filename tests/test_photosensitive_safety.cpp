// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_photosensitive_safety.cpp
/// @brief Phase 10 accessibility coverage for the photosensitivity
///        safe-mode clamp helpers (reduced flashing / shake / strobe /
///        bloom).

#include <gtest/gtest.h>

#include <limits>

#include "accessibility/photosensitive_safety.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-5f;
}

// -- Identity pass-through when safe mode is disabled --

TEST(PhotosensitiveSafety, DisabledIsIdentityForFlashAlpha)
{
    EXPECT_FLOAT_EQ(clampFlashAlpha(0.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(clampFlashAlpha(0.25f, false), 0.25f);
    EXPECT_FLOAT_EQ(clampFlashAlpha(1.0f, false), 1.0f);
}

TEST(PhotosensitiveSafety, DisabledIsIdentityForShakeAmplitude)
{
    EXPECT_FLOAT_EQ(clampShakeAmplitude(0.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(clampShakeAmplitude(1.0f, false), 1.0f);
    EXPECT_FLOAT_EQ(clampShakeAmplitude(42.0f, false), 42.0f);
}

TEST(PhotosensitiveSafety, DisabledIsIdentityForStrobeHz)
{
    EXPECT_FLOAT_EQ(clampStrobeHz(0.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(clampStrobeHz(10.0f, false), 10.0f);
    EXPECT_FLOAT_EQ(clampStrobeHz(60.0f, false), 60.0f);
}

TEST(PhotosensitiveSafety, DisabledIsIdentityForBloomIntensity)
{
    EXPECT_FLOAT_EQ(limitBloomIntensity(0.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(limitBloomIntensity(0.5f, false), 0.5f);
    EXPECT_FLOAT_EQ(limitBloomIntensity(2.0f, false), 2.0f);
}

// -- Default limits — published WCAG / Epilepsy-Society grounded values --

TEST(PhotosensitiveSafety, DefaultLimitsMatchPublishedSafeValues)
{
    PhotosensitiveLimits l;
    EXPECT_NEAR(l.maxFlashAlpha,       0.25f, kEps);
    EXPECT_NEAR(l.shakeAmplitudeScale, 0.25f, kEps);
    EXPECT_NEAR(l.maxStrobeHz,         2.00f, kEps);
    EXPECT_NEAR(l.bloomIntensityScale, 0.60f, kEps);
}

// -- Flash alpha ceiling --

TEST(PhotosensitiveSafety, FlashAlphaClampsToCeiling)
{
    // Values above the ceiling are pulled down.
    EXPECT_NEAR(clampFlashAlpha(1.00f, true), 0.25f, kEps);
    EXPECT_NEAR(clampFlashAlpha(0.80f, true), 0.25f, kEps);
    EXPECT_NEAR(clampFlashAlpha(0.25f, true), 0.25f, kEps);  // at the ceiling
}

TEST(PhotosensitiveSafety, FlashAlphaPassesThroughBelowCeiling)
{
    // Values already safe are not amplified — safe mode only restricts.
    EXPECT_NEAR(clampFlashAlpha(0.10f, true), 0.10f, kEps);
    EXPECT_NEAR(clampFlashAlpha(0.00f, true), 0.00f, kEps);
}

// -- Shake amplitude scaling --

TEST(PhotosensitiveSafety, ShakeAmplitudeScalesByDefaultQuarter)
{
    EXPECT_NEAR(clampShakeAmplitude(1.00f, true), 0.25f, kEps);
    EXPECT_NEAR(clampShakeAmplitude(0.50f, true), 0.125f, kEps);
    EXPECT_NEAR(clampShakeAmplitude(4.00f, true), 1.00f, kEps);
}

TEST(PhotosensitiveSafety, ZeroShakeStaysZero)
{
    // 0 * scale is still 0 — nothing for safe mode to suppress.
    EXPECT_NEAR(clampShakeAmplitude(0.0f, true), 0.0f, kEps);
}

// -- Strobe Hz ceiling --

TEST(PhotosensitiveSafety, StrobeAbove2HzClampsToCeiling)
{
    // WCAG flags >3 Hz as unsafe on red; 2 Hz leaves comfortable margin.
    EXPECT_NEAR(clampStrobeHz(10.0f, true), 2.0f, kEps);
    EXPECT_NEAR(clampStrobeHz(3.5f,  true), 2.0f, kEps);
    EXPECT_NEAR(clampStrobeHz(2.0f,  true), 2.0f, kEps);
}

TEST(PhotosensitiveSafety, StrobeBelow2HzPassesThrough)
{
    EXPECT_NEAR(clampStrobeHz(1.5f, true), 1.5f, kEps);
    EXPECT_NEAR(clampStrobeHz(0.0f, true), 0.0f, kEps);
}

// -- Bloom intensity scaling --

TEST(PhotosensitiveSafety, BloomScalesByDefaultSixTenths)
{
    EXPECT_NEAR(limitBloomIntensity(1.0f, true), 0.60f, kEps);
    EXPECT_NEAR(limitBloomIntensity(0.5f, true), 0.30f, kEps);
    EXPECT_NEAR(limitBloomIntensity(0.1f, true), 0.06f, kEps);
}

// -- Per-caller overrides --

TEST(PhotosensitiveSafety, CustomLimitsOverrideDefaults)
{
    // A scene that wants a tighter cap (e.g. a horror sequence where any
    // flash is too much) can lower the ceiling to 0.10 without rebuilding
    // the engine.
    PhotosensitiveLimits tight;
    tight.maxFlashAlpha       = 0.10f;
    tight.shakeAmplitudeScale = 0.0f;
    tight.maxStrobeHz         = 0.5f;
    tight.bloomIntensityScale = 0.25f;

    EXPECT_NEAR(clampFlashAlpha(1.0f, true, tight), 0.10f, kEps);
    EXPECT_NEAR(clampShakeAmplitude(10.0f, true, tight), 0.0f, kEps);
    EXPECT_NEAR(clampStrobeHz(5.0f, true, tight), 0.5f, kEps);
    EXPECT_NEAR(limitBloomIntensity(1.0f, true, tight), 0.25f, kEps);
}

// -----------------------------------------------------------------------------
// Phase 10.9 Slice 1 F4 — NaN / ±∞ / negative sanitisation in BOTH paths.
//
// WCAG 2.2 Success Criterion 2.3.1 ("Three Flashes or Below Threshold") requires
// that content *cannot* exceed the safe-flashing thresholds. A helper that
// returns NaN, ±∞, or a negative value when fed a bad upstream input silently
// defeats that guarantee — downstream `std::min` / `x * scale` produces
// undefined pixel values that may render as arbitrary brightness, and safe
// mode's presence or absence must not change that sanitisation contract.
//
// Test naming follows the Phase 10.9 preamble pattern:
//     TEST(Photosensitive, WCAG_2_3_1_*)
// so that a grep for the WCAG clause lands on the enforcing tests.
// -----------------------------------------------------------------------------

namespace
{
constexpr float kQuietNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kPosInf   = std::numeric_limits<float>::infinity();
constexpr float kNegInf   = -std::numeric_limits<float>::infinity();
} // namespace

// --- Flash alpha ---

TEST(PhotosensitiveSafety, WCAG_2_3_1_FlashAlphaNaNSanitisedInBothPaths)
{
    EXPECT_FLOAT_EQ(clampFlashAlpha(kQuietNaN, false), 0.0f);
    EXPECT_FLOAT_EQ(clampFlashAlpha(kQuietNaN, true),  0.0f);
}

TEST(PhotosensitiveSafety, WCAG_2_3_1_FlashAlphaInfSanitisedInBothPaths)
{
    EXPECT_FLOAT_EQ(clampFlashAlpha(kPosInf, false), 0.0f);
    EXPECT_FLOAT_EQ(clampFlashAlpha(kPosInf, true),  0.0f);
    EXPECT_FLOAT_EQ(clampFlashAlpha(kNegInf, false), 0.0f);
    EXPECT_FLOAT_EQ(clampFlashAlpha(kNegInf, true),  0.0f);
}

TEST(PhotosensitiveSafety, WCAG_2_3_1_FlashAlphaNegativeClampedToZero)
{
    EXPECT_FLOAT_EQ(clampFlashAlpha(-1.0f,  false), 0.0f);
    EXPECT_FLOAT_EQ(clampFlashAlpha(-0.25f, true),  0.0f);
}

// --- Shake amplitude ---

TEST(PhotosensitiveSafety, WCAG_2_3_1_ShakeAmplitudeNaNSanitisedInBothPaths)
{
    EXPECT_FLOAT_EQ(clampShakeAmplitude(kQuietNaN, false), 0.0f);
    EXPECT_FLOAT_EQ(clampShakeAmplitude(kQuietNaN, true),  0.0f);
}

TEST(PhotosensitiveSafety, WCAG_2_3_1_ShakeAmplitudeInfSanitisedInBothPaths)
{
    EXPECT_FLOAT_EQ(clampShakeAmplitude(kPosInf, false), 0.0f);
    EXPECT_FLOAT_EQ(clampShakeAmplitude(kPosInf, true),  0.0f);
    EXPECT_FLOAT_EQ(clampShakeAmplitude(kNegInf, false), 0.0f);
    EXPECT_FLOAT_EQ(clampShakeAmplitude(kNegInf, true),  0.0f);
}

TEST(PhotosensitiveSafety, WCAG_2_3_1_ShakeAmplitudeNegativeClampedToZero)
{
    EXPECT_FLOAT_EQ(clampShakeAmplitude(-5.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(clampShakeAmplitude(-1.0f, true),  0.0f);
}

// --- Strobe Hz ---

TEST(PhotosensitiveSafety, WCAG_2_3_1_StrobeHzNaNSanitisedInBothPaths)
{
    EXPECT_FLOAT_EQ(clampStrobeHz(kQuietNaN, false), 0.0f);
    EXPECT_FLOAT_EQ(clampStrobeHz(kQuietNaN, true),  0.0f);
}

TEST(PhotosensitiveSafety, WCAG_2_3_1_StrobeHzInfSanitisedInBothPaths)
{
    EXPECT_FLOAT_EQ(clampStrobeHz(kPosInf, false), 0.0f);
    EXPECT_FLOAT_EQ(clampStrobeHz(kPosInf, true),  0.0f);
    EXPECT_FLOAT_EQ(clampStrobeHz(kNegInf, false), 0.0f);
    EXPECT_FLOAT_EQ(clampStrobeHz(kNegInf, true),  0.0f);
}

TEST(PhotosensitiveSafety, WCAG_2_3_1_StrobeHzNegativeClampedToZero)
{
    EXPECT_FLOAT_EQ(clampStrobeHz(-10.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(clampStrobeHz(-1.0f,  true),  0.0f);
}

// --- Bloom intensity ---

TEST(PhotosensitiveSafety, WCAG_2_3_1_BloomIntensityNaNSanitisedInBothPaths)
{
    EXPECT_FLOAT_EQ(limitBloomIntensity(kQuietNaN, false), 0.0f);
    EXPECT_FLOAT_EQ(limitBloomIntensity(kQuietNaN, true),  0.0f);
}

TEST(PhotosensitiveSafety, WCAG_2_3_1_BloomIntensityInfSanitisedInBothPaths)
{
    EXPECT_FLOAT_EQ(limitBloomIntensity(kPosInf, false), 0.0f);
    EXPECT_FLOAT_EQ(limitBloomIntensity(kPosInf, true),  0.0f);
    EXPECT_FLOAT_EQ(limitBloomIntensity(kNegInf, false), 0.0f);
    EXPECT_FLOAT_EQ(limitBloomIntensity(kNegInf, true),  0.0f);
}

TEST(PhotosensitiveSafety, WCAG_2_3_1_BloomIntensityNegativeClampedToZero)
{
    EXPECT_FLOAT_EQ(limitBloomIntensity(-2.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(limitBloomIntensity(-0.5f, true),  0.0f);
}

// --- Regression: finite, non-negative disabled values still pass through.
//
// The existing "DisabledIsIdentityFor*" tests above already pin this for happy
// values; this block pins it for the exact boundary values (0.0f) that the
// F4 sanitiser passes through unmodified, so future refactors can't regress
// the disabled path into a global ceiling.

TEST(PhotosensitiveSafety, WCAG_2_3_1_DisabledPreservesFinitePositives)
{
    EXPECT_FLOAT_EQ(clampFlashAlpha(0.99f, false),     0.99f);
    EXPECT_FLOAT_EQ(clampShakeAmplitude(100.0f, false), 100.0f);
    EXPECT_FLOAT_EQ(clampStrobeHz(60.0f, false),       60.0f);
    EXPECT_FLOAT_EQ(limitBloomIntensity(5.0f, false),   5.0f);
}
