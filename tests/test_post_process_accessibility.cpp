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

// -- Accessibility preset --

TEST(PostProcessAccessibility, SafeDefaultsDisablesEveryMotionSensitiveEffect)
{
    PostProcessAccessibilitySettings s = safeDefaults();
    EXPECT_FALSE(s.depthOfFieldEnabled);
    EXPECT_FALSE(s.motionBlurEnabled);
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
