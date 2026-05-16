// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_taa.cpp
/// @brief Tests for TAA (Temporal Anti-Aliasing) logic.
#include <gtest/gtest.h>
#include "renderer/taa.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <set>

using namespace Vestige;

// =============================================================================
// Halton sequence
// =============================================================================

TEST(TAA, HaltonBase2First8Values)
{
    // Halton(1,2)=0.5, Halton(2,2)=0.25, Halton(3,2)=0.75, Halton(4,2)=0.125
    EXPECT_FLOAT_EQ(Taa::halton(1, 2), 0.5f);
    EXPECT_FLOAT_EQ(Taa::halton(2, 2), 0.25f);
    EXPECT_FLOAT_EQ(Taa::halton(3, 2), 0.75f);
    EXPECT_FLOAT_EQ(Taa::halton(4, 2), 0.125f);
}

TEST(TAA, HaltonBase3First4Values)
{
    EXPECT_NEAR(Taa::halton(1, 3), 1.0f / 3.0f, 1e-5f);
    EXPECT_NEAR(Taa::halton(2, 3), 2.0f / 3.0f, 1e-5f);
    EXPECT_NEAR(Taa::halton(3, 3), 1.0f / 9.0f, 1e-5f);
}

TEST(TAA, HaltonValuesInZeroOneRange)
{
    for (int i = 1; i <= 32; i++)
    {
        float v2 = Taa::halton(i, 2);
        float v3 = Taa::halton(i, 3);
        EXPECT_GE(v2, 0.0f);
        EXPECT_LT(v2, 1.0f);
        EXPECT_GE(v3, 0.0f);
        EXPECT_LT(v3, 1.0f);
    }
}

TEST(TAA, HaltonDoesNotRepeatFor16Samples)
{
    std::set<float> values;
    for (int i = 1; i <= 16; i++)
    {
        float v = Taa::halton(i, 2);
        EXPECT_EQ(values.count(v), 0u) << "Duplicate at index " << i;
        values.insert(v);
    }
}

// Note: jitterProjection / setFeedbackFactor / getFeedbackFactor tests
// require an instance of Taa, which depends on Framebuffer (GL context).
// They are exercised by integration tests rather than this header-only suite.

// Phase 10.9 Slice 18 Ts1 cleanup: dropped `StaticCameraZeroMotion`
// (computed `VP * worldPos` twice with the same VP — tautological)
// and `CameraTranslationProducesMotion` (tested generic GLM
// `glm::lookAt` + perspective projection, not any `Taa::` SUT). The
// motion-vector pipeline is pinned by `test_motion_overlay_prev_world.cpp`.

// =============================================================================
// AntiAliasMode enum
// =============================================================================

TEST(TAA, AntiAliasModeValues)
{
    EXPECT_EQ(static_cast<int>(AntiAliasMode::NONE), 0);
    EXPECT_EQ(static_cast<int>(AntiAliasMode::MSAA_4X), 1);
    EXPECT_EQ(static_cast<int>(AntiAliasMode::TAA), 2);
}
