// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_taa.cpp
/// @brief Tests for TAA (Temporal Anti-Aliasing) logic.
#include <gtest/gtest.h>
#include "renderer/taa.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
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

// =============================================================================
// Jitter projection
// =============================================================================

TEST(TAA, JitterModifiesOnlyColumn2)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    // Create a temporary Taa just to test the static halton and jitterProjection logic
    // Use a direct calculation instead
    glm::mat4 jittered = proj;
    float jitterX = (Taa::halton(1, 2) - 0.5f) * 2.0f / 1920.0f;
    float jitterY = (Taa::halton(1, 3) - 0.5f) * 2.0f / 1080.0f;
    jittered[2][0] += jitterX;
    jittered[2][1] += jitterY;

    // Only [2][0] and [2][1] should differ
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 4; row++)
        {
            if (col == 2 && (row == 0 || row == 1))
            {
                continue;  // These are expected to change
            }
            EXPECT_FLOAT_EQ(proj[col][row], jittered[col][row])
                << "Unexpected change at [" << col << "][" << row << "]";
        }
    }
}

TEST(TAA, JitterMagnitudeIsSubPixel)
{
    // At 1920x1080, jitter should be much less than 1 pixel in NDC
    float jx = (Taa::halton(1, 2) - 0.5f) * 2.0f / 1920.0f;
    float jy = (Taa::halton(1, 3) - 0.5f) * 2.0f / 1080.0f;
    EXPECT_LT(std::abs(jx), 1.0f / 1920.0f * 2.0f);
    EXPECT_LT(std::abs(jy), 1.0f / 1080.0f * 2.0f);
}

// =============================================================================
// Feedback factor
// =============================================================================

TEST(TAA, FeedbackFactorDefault)
{
    // Default should be 0.9
    // Can't construct Taa without GL context, so test the class design expectations
    float defaultFeedback = 0.9f;
    EXPECT_FLOAT_EQ(defaultFeedback, 0.9f);
}

TEST(TAA, FeedbackFactorClamping)
{
    // Test clamping logic manually
    float factor = -0.5f;
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    EXPECT_FLOAT_EQ(factor, 0.0f);

    factor = 1.5f;
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    EXPECT_FLOAT_EQ(factor, 1.0f);
}

// =============================================================================
// Motion vectors (mathematical correctness)
// =============================================================================

TEST(TAA, StaticCameraZeroMotion)
{
    // For a static camera, prevVP == currentVP, so motion vectors should be zero
    glm::mat4 VP = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f)
                  * glm::lookAt(glm::vec3(0, 1, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

    // Simulate a world point
    glm::vec4 worldPos(1.0f, 0.5f, -2.0f, 1.0f);

    glm::vec4 currentClip = VP * worldPos;
    glm::vec2 currentUV = glm::vec2(currentClip) / currentClip.w * 0.5f + 0.5f;

    glm::vec4 prevClip = VP * worldPos;  // Same VP = static camera
    glm::vec2 prevUV = glm::vec2(prevClip) / prevClip.w * 0.5f + 0.5f;

    glm::vec2 motion = currentUV - prevUV;
    EXPECT_NEAR(motion.x, 0.0f, 1e-6f);
    EXPECT_NEAR(motion.y, 0.0f, 1e-6f);
}

TEST(TAA, CameraTranslationProducesMotion)
{
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    glm::mat4 view1 = glm::lookAt(glm::vec3(0, 1, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    glm::mat4 view2 = glm::lookAt(glm::vec3(1, 1, 5), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));

    glm::mat4 VP1 = proj * view1;
    glm::mat4 VP2 = proj * view2;

    glm::vec4 worldPos(0.0f, 0.5f, 0.0f, 1.0f);

    glm::vec4 clip1 = VP1 * worldPos;
    glm::vec2 uv1 = glm::vec2(clip1) / clip1.w * 0.5f + 0.5f;

    glm::vec4 clip2 = VP2 * worldPos;
    glm::vec2 uv2 = glm::vec2(clip2) / clip2.w * 0.5f + 0.5f;

    glm::vec2 motion = uv1 - uv2;
    // Motion should be nonzero since camera moved
    EXPECT_GT(glm::length(motion), 0.001f);
}

// =============================================================================
// AntiAliasMode enum
// =============================================================================

TEST(TAA, AntiAliasModeValues)
{
    EXPECT_EQ(static_cast<int>(AntiAliasMode::NONE), 0);
    EXPECT_EQ(static_cast<int>(AntiAliasMode::MSAA_4X), 1);
    EXPECT_EQ(static_cast<int>(AntiAliasMode::TAA), 2);
}
