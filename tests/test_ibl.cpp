/// @file test_ibl.cpp
/// @brief Unit tests for IBL math: Hammersley sequence, importance sampling, BRDF integration.
#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// =============================================================================
// Reimplement IBL math functions in C++ for testing (mirrors GLSL shader code)
// =============================================================================

static const float PI = 3.14159265359f;

/// Van der Corput radical inverse (bit reversal).
static float radicalInverse_VdC(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

/// Hammersley sequence point.
static glm::vec2 hammersley(uint32_t i, uint32_t N)
{
    return glm::vec2(static_cast<float>(i) / static_cast<float>(N),
                     radicalInverse_VdC(i));
}

/// GGX importance sampling.
static glm::vec3 importanceSampleGGX(glm::vec2 Xi, glm::vec3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0f * PI * Xi.x;
    float cosTheta = std::sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

    glm::vec3 H;
    H.x = std::cos(phi) * sinTheta;
    H.y = std::sin(phi) * sinTheta;
    H.z = cosTheta;

    glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                            : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 tangent = glm::normalize(glm::cross(up, N));
    glm::vec3 bitangent = glm::cross(N, tangent);

    return tangent * H.x + bitangent * H.y + N * H.z;
}

/// Fresnel-Schlick with roughness.
static glm::vec3 fresnelSchlickRoughness(float cosTheta, glm::vec3 F0, float roughness)
{
    return F0 + (glm::max(glm::vec3(1.0f - roughness), F0) - F0)
             * std::pow(glm::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

/// Schlick-GGX geometry for IBL (k = roughness^2 / 2).
static float geometrySchlickGGX_IBL(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

/// Smith's geometry for IBL.
static float geometrySmith_IBL(float NdotV, float NdotL, float roughness)
{
    return geometrySchlickGGX_IBL(NdotV, roughness)
         * geometrySchlickGGX_IBL(NdotL, roughness);
}

/// BRDF integration (split-sum scale and bias).
static glm::vec2 integrateBRDF(float NdotV, float roughness)
{
    glm::vec3 V;
    V.x = std::sqrt(1.0f - NdotV * NdotV);
    V.y = 0.0f;
    V.z = NdotV;

    float A = 0.0f;
    float B = 0.0f;

    glm::vec3 N(0.0f, 0.0f, 1.0f);
    const uint32_t SAMPLE_COUNT = 1024u;

    for (uint32_t i = 0u; i < SAMPLE_COUNT; i++)
    {
        glm::vec2 Xi = hammersley(i, SAMPLE_COUNT);
        glm::vec3 H = importanceSampleGGX(Xi, N, roughness);
        glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

        float NdotL = glm::max(L.z, 0.0f);
        float NdotH = glm::max(H.z, 0.0f);
        float VdotH = glm::max(glm::dot(V, H), 0.0f);

        if (NdotL > 0.0f)
        {
            float G = geometrySmith_IBL(glm::max(V.z, 0.0f), NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * glm::max(V.z, 0.001f));
            float Fc = std::pow(1.0f - VdotH, 5.0f);

            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= static_cast<float>(SAMPLE_COUNT);
    B /= static_cast<float>(SAMPLE_COUNT);

    return glm::vec2(A, B);
}

// =============================================================================
// Hammersley sequence tests
// =============================================================================

TEST(IBLTest, HammersleySequenceInUnitSquare)
{
    const uint32_t N = 256;
    for (uint32_t i = 0; i < N; i++)
    {
        glm::vec2 pt = hammersley(i, N);
        EXPECT_GE(pt.x, 0.0f);
        EXPECT_LE(pt.x, 1.0f);
        EXPECT_GE(pt.y, 0.0f);
        EXPECT_LE(pt.y, 1.0f);
    }
}

TEST(IBLTest, HammersleyFirstPointIsOrigin)
{
    glm::vec2 pt = hammersley(0, 256);
    EXPECT_FLOAT_EQ(pt.x, 0.0f);
    EXPECT_FLOAT_EQ(pt.y, 0.0f);
}

TEST(IBLTest, RadicalInverseProducesUniformDistribution)
{
    // Check that the Van der Corput sequence covers [0,1) without large gaps
    const uint32_t N = 64;
    std::vector<float> values;
    for (uint32_t i = 0; i < N; i++)
    {
        values.push_back(radicalInverse_VdC(i));
    }
    std::sort(values.begin(), values.end());

    // Maximum gap should be roughly 1/N
    float maxGap = 0.0f;
    for (size_t i = 1; i < values.size(); i++)
    {
        maxGap = std::max(maxGap, values[i] - values[i - 1]);
    }
    EXPECT_LT(maxGap, 3.0f / static_cast<float>(N));
}

// =============================================================================
// GGX importance sampling tests
// =============================================================================

TEST(IBLTest, ImportanceSampleGGXProducesUnitVectors)
{
    glm::vec3 N(0.0f, 0.0f, 1.0f);
    for (uint32_t i = 0; i < 100; i++)
    {
        glm::vec2 Xi = hammersley(i, 100);
        glm::vec3 H = importanceSampleGGX(Xi, N, 0.5f);
        float len = glm::length(H);
        EXPECT_NEAR(len, 1.0f, 0.001f);
    }
}

TEST(IBLTest, ImportanceSampleGGXInUpperHemisphere)
{
    glm::vec3 N(0.0f, 0.0f, 1.0f);
    for (uint32_t i = 0; i < 100; i++)
    {
        glm::vec2 Xi = hammersley(i, 100);
        glm::vec3 H = importanceSampleGGX(Xi, N, 0.5f);
        // Half vector should be in the upper hemisphere relative to N
        EXPECT_GE(glm::dot(H, N), -0.01f);
    }
}

TEST(IBLTest, SmoothSurfaceConcentratesSamplesNearNormal)
{
    glm::vec3 N(0.0f, 0.0f, 1.0f);
    float avgDot = 0.0f;
    const uint32_t samples = 512;
    for (uint32_t i = 0; i < samples; i++)
    {
        glm::vec2 Xi = hammersley(i, samples);
        glm::vec3 H = importanceSampleGGX(Xi, N, 0.01f);  // Very smooth
        avgDot += glm::dot(H, N);
    }
    avgDot /= static_cast<float>(samples);
    // For very smooth surfaces, samples cluster around N (avgDot close to 1)
    EXPECT_GT(avgDot, 0.95f);
}

TEST(IBLTest, RoughSurfaceSpreadssamples)
{
    glm::vec3 N(0.0f, 0.0f, 1.0f);
    float avgDotSmooth = 0.0f;
    float avgDotRough = 0.0f;
    const uint32_t samples = 512;

    for (uint32_t i = 0; i < samples; i++)
    {
        glm::vec2 Xi = hammersley(i, samples);
        avgDotSmooth += glm::dot(importanceSampleGGX(Xi, N, 0.1f), N);
        avgDotRough += glm::dot(importanceSampleGGX(Xi, N, 0.9f), N);
    }
    avgDotSmooth /= static_cast<float>(samples);
    avgDotRough /= static_cast<float>(samples);

    // Rough surface should spread more (lower avg dot with normal)
    EXPECT_GT(avgDotSmooth, avgDotRough);
}

// =============================================================================
// BRDF LUT tests
// =============================================================================

TEST(IBLTest, BrdfLutHeadOnSmoothSurface)
{
    // NdotV = 1.0, roughness near 0: scale should be close to 1, bias near 0
    glm::vec2 result = integrateBRDF(1.0f, 0.01f);
    EXPECT_GT(result.x, 0.8f);  // Scale (high for head-on smooth)
    EXPECT_LT(result.y, 0.15f); // Bias (low for head-on)
}

TEST(IBLTest, BrdfLutGrazingSmoothSurface)
{
    // NdotV near 0, roughness near 0: strong Fresnel → high bias
    glm::vec2 result = integrateBRDF(0.05f, 0.01f);
    // At grazing angles, Fc = pow(1 - VdotH, 5) is large → bias increases
    EXPECT_GT(result.x + result.y, 0.0f);  // Combined should be positive
}

TEST(IBLTest, BrdfLutValuesInRange)
{
    // Sample several points and verify they're in valid range
    for (float ndotv = 0.1f; ndotv <= 1.0f; ndotv += 0.2f)
    {
        for (float roughness = 0.05f; roughness <= 1.0f; roughness += 0.2f)
        {
            glm::vec2 result = integrateBRDF(ndotv, roughness);
            EXPECT_GE(result.x, 0.0f) << "Scale negative at NdotV=" << ndotv
                                       << " roughness=" << roughness;
            EXPECT_LE(result.x, 1.5f) << "Scale too high at NdotV=" << ndotv
                                       << " roughness=" << roughness;
            EXPECT_GE(result.y, 0.0f) << "Bias negative at NdotV=" << ndotv
                                       << " roughness=" << roughness;
            EXPECT_LE(result.y, 1.5f) << "Bias too high at NdotV=" << ndotv
                                       << " roughness=" << roughness;
        }
    }
}

TEST(IBLTest, BrdfLutRoughnessMonotonic)
{
    // At head-on (NdotV=1), increasing roughness should generally decrease scale
    glm::vec2 smooth = integrateBRDF(1.0f, 0.1f);
    glm::vec2 rough = integrateBRDF(1.0f, 0.9f);
    EXPECT_GT(smooth.x, rough.x);
}

// =============================================================================
// Fresnel-Schlick roughness tests
// =============================================================================

TEST(IBLTest, FresnelSchlickRoughnessHeadOn)
{
    // At head-on (cosTheta = 1), F = F0 regardless of roughness
    glm::vec3 F0(0.04f);
    glm::vec3 result = fresnelSchlickRoughness(1.0f, F0, 0.5f);
    EXPECT_NEAR(result.x, 0.04f, 0.001f);
    EXPECT_NEAR(result.y, 0.04f, 0.001f);
    EXPECT_NEAR(result.z, 0.04f, 0.001f);
}

TEST(IBLTest, FresnelSchlickRoughnessGrazingSmooth)
{
    // At grazing angle with smooth surface, F → 1
    glm::vec3 F0(0.04f);
    glm::vec3 result = fresnelSchlickRoughness(0.0f, F0, 0.0f);
    EXPECT_NEAR(result.x, 1.0f, 0.01f);
    EXPECT_NEAR(result.y, 1.0f, 0.01f);
    EXPECT_NEAR(result.z, 1.0f, 0.01f);
}

TEST(IBLTest, FresnelSchlickRoughnessGrazingRough)
{
    // At grazing angle with rough surface, F is capped by roughness
    glm::vec3 F0(0.04f);
    glm::vec3 result = fresnelSchlickRoughness(0.0f, F0, 0.9f);
    // max(1.0 - 0.9, 0.04) = max(0.1, 0.04) = 0.1
    EXPECT_NEAR(result.x, 0.1f, 0.01f);
}

// =============================================================================
// Geometry function tests
// =============================================================================

TEST(IBLTest, GeometrySchlickGGXHeadOn)
{
    // NdotV = 1.0: geometry term should be close to 1.0 (no self-shadowing)
    float G = geometrySchlickGGX_IBL(1.0f, 0.5f);
    EXPECT_GT(G, 0.8f);
    EXPECT_LE(G, 1.0f);
}

TEST(IBLTest, GeometrySchlickGGXGrazing)
{
    // NdotV near 0: strong self-shadowing
    float G = geometrySchlickGGX_IBL(0.01f, 0.5f);
    EXPECT_LT(G, 0.1f);
    EXPECT_GE(G, 0.0f);
}

TEST(IBLTest, GeometrySmithSymmetric)
{
    // G(V, L) should be symmetric: same NdotV and NdotL gives same result
    float G1 = geometrySmith_IBL(0.5f, 0.8f, 0.3f);
    float G2 = geometrySmith_IBL(0.8f, 0.5f, 0.3f);
    EXPECT_FLOAT_EQ(G1, G2);
}
