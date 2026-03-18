/// @file test_ssao.cpp
/// @brief Unit tests for SSAO math (kernel generation, depth linearization, occlusion logic).
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <random>
#include <vector>

// --- SSAO kernel generation (mirroring renderer implementation) ---

static std::vector<glm::vec3> generateSsaoKernel(int kernelSize)
{
    std::vector<glm::vec3> kernel;
    std::mt19937 gen(42);  // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < kernelSize; i++)
    {
        // Random point in hemisphere (z >= 0)
        glm::vec3 sample(
            dist(gen) * 2.0f - 1.0f,
            dist(gen) * 2.0f - 1.0f,
            dist(gen));

        sample = glm::normalize(sample);
        sample *= dist(gen);

        // Bias toward surface: scale by lerp(0.1, 1.0, (i/kernelSize)^2)
        float scale = static_cast<float>(i) / static_cast<float>(kernelSize);
        scale = 0.1f + scale * scale * (1.0f - 0.1f);
        sample *= scale;

        kernel.push_back(sample);
    }
    return kernel;
}

// --- Depth linearization (mirroring shader implementation) ---

static float linearizeDepth(float depth, float nearPlane, float farPlane)
{
    float z = depth * 2.0f - 1.0f;  // NDC
    return (2.0f * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
}

// =============================================================================
// Kernel sample tests
// =============================================================================

TEST(SSAOTest, KernelSamplesInHemisphere)
{
    auto kernel = generateSsaoKernel(64);
    for (const auto& sample : kernel)
    {
        // All samples must have z >= 0 (hemisphere)
        EXPECT_GE(sample.z, 0.0f);
    }
}

TEST(SSAOTest, KernelSamplesNonZero)
{
    auto kernel = generateSsaoKernel(64);
    for (const auto& sample : kernel)
    {
        float len = glm::length(sample);
        EXPECT_GT(len, 0.0f);
    }
}

TEST(SSAOTest, KernelBiasTowardSurface)
{
    auto kernel = generateSsaoKernel(64);

    // Early samples (near surface) should generally be shorter than late samples
    // Compare average length of first quarter vs last quarter
    float earlyAvg = 0.0f;
    float lateAvg = 0.0f;
    int quarter = 64 / 4;

    for (int i = 0; i < quarter; i++)
    {
        earlyAvg += glm::length(kernel[static_cast<size_t>(i)]);
    }
    for (int i = 64 - quarter; i < 64; i++)
    {
        lateAvg += glm::length(kernel[static_cast<size_t>(i)]);
    }

    earlyAvg /= static_cast<float>(quarter);
    lateAvg /= static_cast<float>(quarter);

    EXPECT_LT(earlyAvg, lateAvg);
}

TEST(SSAOTest, KernelSampleCountCorrect)
{
    auto kernel = generateSsaoKernel(64);
    EXPECT_EQ(static_cast<int>(kernel.size()), 64);
}

// =============================================================================
// Depth linearization tests
// =============================================================================

TEST(SSAOTest, LinearizeDepthNearPlane)
{
    // Depth 0.0 (near plane) should linearize to near plane distance
    float linear = linearizeDepth(0.0f, 0.1f, 100.0f);
    EXPECT_NEAR(linear, 0.1f, 0.01f);
}

TEST(SSAOTest, LinearizeDepthFarPlane)
{
    // Depth 1.0 (far plane) should linearize to far plane distance
    float linear = linearizeDepth(1.0f, 0.1f, 100.0f);
    EXPECT_NEAR(linear, 100.0f, 0.1f);
}

TEST(SSAOTest, LinearizeDepthMonotonic)
{
    // Linearized depth should be monotonically increasing
    float prev = linearizeDepth(0.0f, 0.1f, 100.0f);
    for (int i = 1; i <= 10; i++)
    {
        float d = static_cast<float>(i) / 10.0f;
        float curr = linearizeDepth(d, 0.1f, 100.0f);
        EXPECT_GT(curr, prev);
        prev = curr;
    }
}

// =============================================================================
// Occlusion logic tests
// =============================================================================

TEST(SSAOTest, FullOcclusionIsZero)
{
    // When all samples are occluded, AO factor = 0.0
    int kernelSize = 64;
    float occlusion = static_cast<float>(kernelSize);
    float ao = 1.0f - (occlusion / static_cast<float>(kernelSize));
    EXPECT_FLOAT_EQ(ao, 0.0f);
}

TEST(SSAOTest, NoOcclusionIsOne)
{
    // When no samples are occluded, AO factor = 1.0
    float occlusion = 0.0f;
    int kernelSize = 64;
    float ao = 1.0f - (occlusion / static_cast<float>(kernelSize));
    EXPECT_FLOAT_EQ(ao, 1.0f);
}

TEST(SSAOTest, HalfOcclusionIsHalf)
{
    // When half the samples are occluded, AO factor = 0.5
    int kernelSize = 64;
    float occlusion = static_cast<float>(kernelSize) / 2.0f;
    float ao = 1.0f - (occlusion / static_cast<float>(kernelSize));
    EXPECT_FLOAT_EQ(ao, 0.5f);
}
