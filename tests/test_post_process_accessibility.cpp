// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_post_process_accessibility.cpp
/// @brief Phase 10 accessibility coverage for the depth-of-field +
///        motion-blur toggles and the one-click accessibility preset.

#include <gtest/gtest.h>

#include "accessibility/post_process_accessibility.h"

using namespace Vestige;

// -- Defaults --

TEST(PostProcessAccessibility, BothEffectsDefaultOn)
{
    // Vestige defaults both effects on for visual quality; users opt
    // out individually or via the Accessibility preset. If either
    // ever silently flips its default, a ROADMAP + CHANGELOG trail
    // must explain why — surface the regression here.
    PostProcessAccessibilitySettings s;
    EXPECT_TRUE(s.depthOfFieldEnabled);
    EXPECT_TRUE(s.motionBlurEnabled);
}

TEST(PostProcessAccessibility, FogDefaultsOnFullIntensityNoReducedMotion)
{
    // Fog is normal visual polish — default on, full authored density,
    // reduced-motion off. Users opt out via explicit toggle or
    // safeDefaults.
    PostProcessAccessibilitySettings s;
    EXPECT_TRUE(s.fogEnabled);
    EXPECT_FLOAT_EQ(s.fogIntensityScale, 1.0f);
    EXPECT_FALSE(s.reduceMotionFog);
}

// -- Accessibility preset --

TEST(PostProcessAccessibility, SafeDefaultsDisablesEveryMotionSensitiveEffect)
{
    PostProcessAccessibilitySettings s = safeDefaults();
    EXPECT_FALSE(s.depthOfFieldEnabled);
    EXPECT_FALSE(s.motionBlurEnabled);
}

TEST(PostProcessAccessibility, SafeDefaultsKeepsFogOnAtHalfIntensityWithReducedMotion)
{
    // Turning distance fog off entirely creates a harsh horizon cutoff
    // that's visually worse for low-contrast-sensitivity users than
    // having fog at all. The safe preset therefore keeps fogEnabled
    // on but halves intensity + enables reduced-motion mode so the
    // sun-inscatter lobe and (future) volumetric temporal reprojection
    // can't cause photosensitivity flashes.
    PostProcessAccessibilitySettings s = safeDefaults();
    EXPECT_TRUE(s.fogEnabled);
    EXPECT_FLOAT_EQ(s.fogIntensityScale, 0.5f);
    EXPECT_TRUE(s.reduceMotionFog);
}

TEST(PostProcessAccessibility, SafeDefaultsIsDistinctFromZeroInitDefault)
{
    // The "preset" and the bare-struct default must be inequivalent —
    // otherwise the user-visible "Accessibility preset" button is a
    // no-op, which would be a silent regression.
    PostProcessAccessibilitySettings bare;
    PostProcessAccessibilitySettings safe = safeDefaults();
    EXPECT_NE(bare, safe);
}

// -- Equality --

TEST(PostProcessAccessibility, EqualityMatchesAllFields)
{
    PostProcessAccessibilitySettings a;
    PostProcessAccessibilitySettings b;
    EXPECT_EQ(a, b);

    b.depthOfFieldEnabled = false;
    EXPECT_NE(a, b);

    a.depthOfFieldEnabled = false;
    EXPECT_EQ(a, b);

    b.motionBlurEnabled = false;
    EXPECT_NE(a, b);

    a.motionBlurEnabled = false;
    EXPECT_EQ(a, b);

    b.fogEnabled = false;
    EXPECT_NE(a, b);

    a.fogEnabled = false;
    EXPECT_EQ(a, b);

    b.fogIntensityScale = 0.25f;
    EXPECT_NE(a, b);

    a.fogIntensityScale = 0.25f;
    EXPECT_EQ(a, b);

    b.reduceMotionFog = true;
    EXPECT_NE(a, b);
}

// -- Per-field toggle --

TEST(PostProcessAccessibility, IndividualTogglesAreIndependent)
{
    // A user who gets migraines from DoF but not motion blur (or
    // vice versa) can toggle one without losing the other.
    PostProcessAccessibilitySettings s;
    s.depthOfFieldEnabled = false;
    EXPECT_FALSE(s.depthOfFieldEnabled);
    EXPECT_TRUE(s.motionBlurEnabled);

    s = {};
    s.motionBlurEnabled = false;
    EXPECT_TRUE(s.depthOfFieldEnabled);
    EXPECT_FALSE(s.motionBlurEnabled);
}

TEST(PostProcessAccessibility, FogTogglesAreIndependent)
{
    // A user might want fog overall but reduced motion only, or full
    // intensity distance fog without sun-lobe flashing. Verify each
    // flag mutates in isolation.
    PostProcessAccessibilitySettings s;
    s.fogEnabled = false;
    EXPECT_FALSE(s.fogEnabled);
    EXPECT_FLOAT_EQ(s.fogIntensityScale, 1.0f);
    EXPECT_FALSE(s.reduceMotionFog);

    s = {};
    s.fogIntensityScale = 0.3f;
    EXPECT_TRUE(s.fogEnabled);
    EXPECT_FLOAT_EQ(s.fogIntensityScale, 0.3f);
    EXPECT_FALSE(s.reduceMotionFog);

    s = {};
    s.reduceMotionFog = true;
    EXPECT_TRUE(s.fogEnabled);
    EXPECT_FLOAT_EQ(s.fogIntensityScale, 1.0f);
    EXPECT_TRUE(s.reduceMotionFog);
}
