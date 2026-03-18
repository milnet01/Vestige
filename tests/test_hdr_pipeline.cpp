/// @file test_hdr_pipeline.cpp
/// @brief Unit tests for the HDR rendering pipeline math (tone mapping, gamma correction).
///
/// Note: Renderer state tests (exposure/tonemapMode/debugMode getters/setters) require
/// an OpenGL context and are verified through runtime testing. These tests validate
/// the mathematical correctness of the tonemapper implementations and gamma correction.
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>

// --- Tonemapper math functions (mirroring the shader implementations) ---

static glm::vec3 tonemapReinhard(glm::vec3 color)
{
    return color / (color + glm::vec3(1.0f));
}

static glm::vec3 tonemapACES(glm::vec3 color)
{
    return (color * (2.51f * color + 0.03f)) / (color * (2.43f * color + 0.59f) + 0.14f);
}

static float gammaCorrect(float linear)
{
    return std::pow(linear, 1.0f / 2.2f);
}

// --- Reinhard math verification ---

TEST(HDRPipelineTest, ReinhardUnitInput)
{
    // Reinhard(1.0) = 1.0 / (1.0 + 1.0) = 0.5
    glm::vec3 result = tonemapReinhard(glm::vec3(1.0f));
    EXPECT_FLOAT_EQ(result.r, 0.5f);
    EXPECT_FLOAT_EQ(result.g, 0.5f);
    EXPECT_FLOAT_EQ(result.b, 0.5f);
}

TEST(HDRPipelineTest, ReinhardZeroInput)
{
    // Reinhard(0.0) = 0.0 / (0.0 + 1.0) = 0.0
    glm::vec3 result = tonemapReinhard(glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(result.r, 0.0f);
    EXPECT_FLOAT_EQ(result.g, 0.0f);
    EXPECT_FLOAT_EQ(result.b, 0.0f);
}

TEST(HDRPipelineTest, ReinhardHighInput)
{
    // Reinhard should compress large values toward 1.0 without exceeding it
    glm::vec3 result = tonemapReinhard(glm::vec3(100.0f));
    EXPECT_GT(result.r, 0.99f);
    EXPECT_LT(result.r, 1.0f);
}

TEST(HDRPipelineTest, ReinhardPreservesColorRatios)
{
    // Different channel values should maintain their relative ordering
    glm::vec3 input(0.5f, 1.0f, 2.0f);
    glm::vec3 result = tonemapReinhard(input);
    EXPECT_LT(result.r, result.g);
    EXPECT_LT(result.g, result.b);
}

// --- ACES math verification ---

TEST(HDRPipelineTest, ACESKnownValue)
{
    // ACES at x=1.0:
    // (1.0 * (2.51 + 0.03)) / (1.0 * (2.43 + 0.59) + 0.14)
    // = 2.54 / 3.16 ≈ 0.8038
    glm::vec3 result = tonemapACES(glm::vec3(1.0f));
    EXPECT_NEAR(result.r, 0.8038f, 0.001f);
    EXPECT_NEAR(result.g, 0.8038f, 0.001f);
    EXPECT_NEAR(result.b, 0.8038f, 0.001f);
}

TEST(HDRPipelineTest, ACESZeroInput)
{
    // ACES(0.0) = (0 * 0.03) / (0 * 0.59 + 0.14) = 0.0 / 0.14 = 0.0
    glm::vec3 result = tonemapACES(glm::vec3(0.0f));
    EXPECT_NEAR(result.r, 0.0f, 0.001f);
    EXPECT_NEAR(result.g, 0.0f, 0.001f);
    EXPECT_NEAR(result.b, 0.0f, 0.001f);
}

TEST(HDRPipelineTest, ACESHighInputApproachesOne)
{
    // ACES should approach ~1.0 for very large inputs
    // Limit: 2.51 / 2.43 ≈ 1.033 — the curve slightly overshoots 1.0
    glm::vec3 result = tonemapACES(glm::vec3(100.0f));
    EXPECT_NEAR(result.r, 2.51f / 2.43f, 0.01f);
}

// --- Gamma correction verification ---

TEST(HDRPipelineTest, GammaCorrectionMidGray)
{
    // pow(0.5, 1/2.2) ≈ 0.7297
    float srgb = gammaCorrect(0.5f);
    EXPECT_NEAR(srgb, 0.7297f, 0.001f);
}

TEST(HDRPipelineTest, GammaCorrectionBlack)
{
    // pow(0.0, 1/2.2) = 0.0
    float srgb = gammaCorrect(0.0f);
    EXPECT_FLOAT_EQ(srgb, 0.0f);
}

TEST(HDRPipelineTest, GammaCorrectionWhite)
{
    // pow(1.0, 1/2.2) = 1.0
    float srgb = gammaCorrect(1.0f);
    EXPECT_FLOAT_EQ(srgb, 1.0f);
}

TEST(HDRPipelineTest, GammaCorrectionBrightensLinear)
{
    // Gamma correction should make mid-tones brighter (sRGB > linear for 0 < x < 1)
    float linear = 0.25f;
    float srgb = gammaCorrect(linear);
    EXPECT_GT(srgb, linear);
}

// --- Luminance calculation ---

TEST(HDRPipelineTest, LuminanceWhite)
{
    // Luminance of white (1,1,1) = 0.2126 + 0.7152 + 0.0722 = 1.0
    glm::vec3 white(1.0f);
    float luminance = glm::dot(white, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    EXPECT_NEAR(luminance, 1.0f, 0.001f);
}

TEST(HDRPipelineTest, LuminancePureGreenIsBrightest)
{
    // Green has the highest luminance coefficient (0.7152)
    glm::vec3 coefficients(0.2126f, 0.7152f, 0.0722f);
    float redLum = glm::dot(glm::vec3(1.0f, 0.0f, 0.0f), coefficients);
    float greenLum = glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), coefficients);
    float blueLum = glm::dot(glm::vec3(0.0f, 0.0f, 1.0f), coefficients);
    EXPECT_GT(greenLum, redLum);
    EXPECT_GT(greenLum, blueLum);
}
