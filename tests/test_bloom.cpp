// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_bloom.cpp
/// @brief Headless CPU oracles for bloom math that doesn't need a GL
///        context. Numerical CPU↔GPU parity for the actual shader helpers
///        (`bt709Luminance`, `karisWeight`, `softThreshold`) lives in
///        `tests/test_bloom_parity.cpp` — this file covers a few cheap
///        invariants that are still useful as a first-line headless check
///        (luminance ratios, additive composite contract).
///
/// Removed 2026-05-02:
///   - `brightPass` + 3 tests — asserted a *hard*-threshold contract
///     (`luma > t ? color : 0`); production shader does *soft* threshold
///     with a `contrib / (contrib + 1.0)` knee. Real coverage now via
///     `test_bloom_parity.cpp::SoftThresholdMatchesCpuAcrossLumaBands`.
///   - `GAUSSIAN_WEIGHTS` + 2 tests — referenced a 5-tap Gaussian array
///     that no current bloom shader uses. Downsample is 13-tap Karis-
///     weighted box, upsample is a 3×3 tent.
#include <gtest/gtest.h>
#include <glm/glm.hpp>

// --- BT.709 luminance (mirroring shader implementation) ---

static float bt709Luminance(const glm::vec3& color)
{
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

// --- Bloom composite (mirroring screen_quad.frag.glsl: color += bloom * intensity) ---

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
