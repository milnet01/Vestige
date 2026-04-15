// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_bloom.cpp
/// @brief Unit tests for bloom post-processing math (luminance, thresholding, blur weights, composite).
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>

// --- BT.709 luminance (mirroring shader implementation) ---

static float bt709Luminance(const glm::vec3& color)
{
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

// --- Bright pass threshold (mirroring shader implementation) ---

static glm::vec3 brightPass(const glm::vec3& color, float threshold)
{
    float luminance = bt709Luminance(color);
    if (luminance > threshold)
    {
        return color;
    }
    return glm::vec3(0.0f);
}

// --- Gaussian weights (mirroring shader implementation) ---

static const float GAUSSIAN_WEIGHTS[5] = {0.227027f, 0.1945946f, 0.1216216f, 0.054054f, 0.016216f};

// --- Bloom composite (mirroring shader implementation) ---

static glm::vec3 bloomComposite(const glm::vec3& scene, const glm::vec3& bloom, float intensity)
{
    return scene + bloom * intensity;
}

// =============================================================================
// BT.709 Luminance tests
// =============================================================================

TEST(BloomTest, LuminanceWhiteIsOne)
{
    float lum = bt709Luminance(glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_NEAR(lum, 1.0f, 0.001f);
}

TEST(BloomTest, LuminanceBlackIsZero)
{
    float lum = bt709Luminance(glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(lum, 0.0f);
}

TEST(BloomTest, LuminanceGreenIsBrightest)
{
    float redLum = bt709Luminance(glm::vec3(1.0f, 0.0f, 0.0f));
    float greenLum = bt709Luminance(glm::vec3(0.0f, 1.0f, 0.0f));
    float blueLum = bt709Luminance(glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_GT(greenLum, redLum);
    EXPECT_GT(greenLum, blueLum);
}

// =============================================================================
// Bright pass threshold tests
// =============================================================================

TEST(BloomTest, BrightPassBelowThresholdIsBlack)
{
    // Color with luminance ~0.5 should be black with threshold 1.0
    glm::vec3 result = brightPass(glm::vec3(0.5f, 0.5f, 0.5f), 1.0f);
    EXPECT_FLOAT_EQ(result.r, 0.0f);
    EXPECT_FLOAT_EQ(result.g, 0.0f);
    EXPECT_FLOAT_EQ(result.b, 0.0f);
}

TEST(BloomTest, BrightPassAboveThresholdPassesThrough)
{
    // Color with luminance ~2.0 should pass through with threshold 1.0
    glm::vec3 input(2.0f, 2.0f, 2.0f);
    glm::vec3 result = brightPass(input, 1.0f);
    EXPECT_FLOAT_EQ(result.r, 2.0f);
    EXPECT_FLOAT_EQ(result.g, 2.0f);
    EXPECT_FLOAT_EQ(result.b, 2.0f);
}

TEST(BloomTest, BrightPassAtThresholdIsBlack)
{
    // Exactly at threshold — not strictly greater, so should be black
    glm::vec3 input(1.0f, 1.0f, 1.0f);
    glm::vec3 result = brightPass(input, 1.0f);
    EXPECT_FLOAT_EQ(result.r, 0.0f);
    EXPECT_FLOAT_EQ(result.g, 0.0f);
    EXPECT_FLOAT_EQ(result.b, 0.0f);
}

// =============================================================================
// Gaussian weight tests
// =============================================================================

TEST(BloomTest, GaussianWeightsSumToOne)
{
    // Center weight + 2 * (each side weight)
    float sum = GAUSSIAN_WEIGHTS[0];
    for (int i = 1; i < 5; i++)
    {
        sum += 2.0f * GAUSSIAN_WEIGHTS[i];
    }
    EXPECT_NEAR(sum, 1.0f, 0.001f);
}

TEST(BloomTest, GaussianWeightsMonotonicallyDecreasing)
{
    for (int i = 0; i < 4; i++)
    {
        EXPECT_GT(GAUSSIAN_WEIGHTS[i], GAUSSIAN_WEIGHTS[i + 1]);
    }
}

// =============================================================================
// Bloom composite tests
// =============================================================================

TEST(BloomTest, CompositeIsAdditive)
{
    glm::vec3 scene(0.5f, 0.3f, 0.1f);
    glm::vec3 bloom(0.2f, 0.4f, 0.6f);
    float intensity = 1.0f;
    glm::vec3 result = bloomComposite(scene, bloom, intensity);
    EXPECT_FLOAT_EQ(result.r, 0.7f);
    EXPECT_FLOAT_EQ(result.g, 0.7f);
    EXPECT_FLOAT_EQ(result.b, 0.7f);
}

TEST(BloomTest, CompositeWithZeroIntensityEqualsScene)
{
    glm::vec3 scene(0.5f, 0.5f, 0.5f);
    glm::vec3 bloom(1.0f, 1.0f, 1.0f);
    glm::vec3 result = bloomComposite(scene, bloom, 0.0f);
    EXPECT_FLOAT_EQ(result.r, scene.r);
    EXPECT_FLOAT_EQ(result.g, scene.g);
    EXPECT_FLOAT_EQ(result.b, scene.b);
}

TEST(BloomTest, CompositeScalesWithIntensity)
{
    glm::vec3 scene(0.0f);
    glm::vec3 bloom(1.0f, 1.0f, 1.0f);
    glm::vec3 result = bloomComposite(scene, bloom, 0.5f);
    EXPECT_FLOAT_EQ(result.r, 0.5f);
    EXPECT_FLOAT_EQ(result.g, 0.5f);
    EXPECT_FLOAT_EQ(result.b, 0.5f);
}
