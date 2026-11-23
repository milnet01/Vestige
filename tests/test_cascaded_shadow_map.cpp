// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cascaded_shadow_map.cpp
/// @brief Unit tests for cascaded shadow map frustum splitting and matrix computation.
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "renderer/cascaded_shadow_map.h"

using namespace Vestige;

// =============================================================================
// Split distance tests
// =============================================================================

TEST(CascadedShadowMapTest, SplitDistancesMonotonicallyIncreasing)
{
    auto splits = CascadedShadowMap::computeSplitDistances(0.1f, 150.0f, 4, 0.5f);
    ASSERT_EQ(splits.size(), 4u);

    EXPECT_GT(splits[0], 0.1f);
    for (size_t i = 1; i < splits.size(); i++)
    {
        EXPECT_GT(splits[i], splits[i - 1]);
    }
}

TEST(CascadedShadowMapTest, SplitDistancesWithinRange)
{
    float near = 0.1f;
    float far = 200.0f;
    auto splits = CascadedShadowMap::computeSplitDistances(near, far, 4, 0.5f);

    for (float s : splits)
    {
        EXPECT_GE(s, near);
        EXPECT_LE(s, far);
    }
}

TEST(CascadedShadowMapTest, LastSplitEqualsFarPlane)
{
    auto splits = CascadedShadowMap::computeSplitDistances(0.1f, 100.0f, 4, 0.5f);
    EXPECT_FLOAT_EQ(splits.back(), 100.0f);
}

TEST(CascadedShadowMapTest, PureLinearSplits)
{
    float near = 1.0f;
    float far = 100.0f;
    auto splits = CascadedShadowMap::computeSplitDistances(near, far, 4, 0.0f);

    // Linear: near + (far - near) * (i+1)/N
    for (int i = 0; i < 4; i++)
    {
        float expected = near + (far - near) * static_cast<float>(i + 1) / 4.0f;
        EXPECT_NEAR(splits[static_cast<size_t>(i)], expected, 0.001f);
    }
}

TEST(CascadedShadowMapTest, PureLogarithmicSplits)
{
    float near = 1.0f;
    float far = 100.0f;
    auto splits = CascadedShadowMap::computeSplitDistances(near, far, 4, 1.0f);

    // Logarithmic: near * (far/near)^((i+1)/N)
    for (int i = 0; i < 4; i++)
    {
        float p = static_cast<float>(i + 1) / 4.0f;
        float expected = near * std::pow(far / near, p);
        EXPECT_NEAR(splits[static_cast<size_t>(i)], expected, 0.001f);
    }
}

TEST(CascadedShadowMapTest, LogarithmicFrontLoaded)
{
    float near = 0.1f;
    float far = 150.0f;
    auto logSplits = CascadedShadowMap::computeSplitDistances(near, far, 4, 1.0f);
    auto linSplits = CascadedShadowMap::computeSplitDistances(near, far, 4, 0.0f);

    // Logarithmic puts more resolution near the camera (smaller first split)
    EXPECT_LT(logSplits[0], linSplits[0]);
}

TEST(CascadedShadowMapTest, SingleCascadeSplit)
{
    auto splits = CascadedShadowMap::computeSplitDistances(0.1f, 100.0f, 1, 0.5f);
    ASSERT_EQ(splits.size(), 1u);
    EXPECT_FLOAT_EQ(splits[0], 100.0f);
}

TEST(CascadedShadowMapTest, CascadeCountConfigurations)
{
    for (int n = 1; n <= 4; n++)
    {
        auto splits = CascadedShadowMap::computeSplitDistances(0.1f, 100.0f, n, 0.5f);
        EXPECT_EQ(static_cast<int>(splits.size()), n);
    }
}

// =============================================================================
// Frustum corner tests
// =============================================================================

TEST(CascadedShadowMapTest, FrustumCornersCount)
{
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

    auto corners = CascadedShadowMap::computeFrustumCorners(proj * view);
    EXPECT_EQ(corners.size(), 8u);
}

TEST(CascadedShadowMapTest, FrustumCornersAreFinite)
{
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 10.0f);

    auto corners = CascadedShadowMap::computeFrustumCorners(proj * view);

    for (const auto& c : corners)
    {
        EXPECT_FALSE(std::isnan(c.x));
        EXPECT_FALSE(std::isnan(c.y));
        EXPECT_FALSE(std::isnan(c.z));
        EXPECT_FALSE(std::isinf(c.x));
        EXPECT_FALSE(std::isinf(c.y));
        EXPECT_FALSE(std::isinf(c.z));
    }
}

TEST(CascadedShadowMapTest, FrustumCornersFormNonZeroVolume)
{
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 10.0f);

    auto corners = CascadedShadowMap::computeFrustumCorners(proj * view);

    glm::vec3 bboxMin(std::numeric_limits<float>::max());
    glm::vec3 bboxMax(std::numeric_limits<float>::lowest());
    for (const auto& c : corners)
    {
        bboxMin = glm::min(bboxMin, c);
        bboxMax = glm::max(bboxMax, c);
    }

    glm::vec3 extent = bboxMax - bboxMin;
    EXPECT_GT(extent.x, 0.0f);
    EXPECT_GT(extent.y, 0.0f);
    EXPECT_GT(extent.z, 0.0f);
}

TEST(CascadedShadowMapTest, NarrowFrustumSmallerThanWideFrustum)
{
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 narrowProj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 5.0f);
    glm::mat4 wideProj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 50.0f);

    auto narrowCorners = CascadedShadowMap::computeFrustumCorners(narrowProj * view);
    auto wideCorners = CascadedShadowMap::computeFrustumCorners(wideProj * view);

    auto computeVolume = [](const std::array<glm::vec3, 8>& corners) -> float
    {
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());
        for (const auto& c : corners)
        {
            mn = glm::min(mn, c);
            mx = glm::max(mx, c);
        }
        glm::vec3 ext = mx - mn;
        return ext.x * ext.y * ext.z;
    };

    EXPECT_LT(computeVolume(narrowCorners), computeVolume(wideCorners));
}

// =============================================================================
// Light-space matrix tests
// =============================================================================

TEST(CascadedShadowMapTest, CascadeMatrixEncompassesFrustum)
{
    DirectionalLight light;
    light.direction = glm::vec3(-0.2f, -1.0f, -0.3f);

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 5.0f, 10.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 20.0f);

    auto corners = CascadedShadowMap::computeFrustumCorners(proj * view);
    glm::mat4 cascadeMatrix = CascadedShadowMap::computeCascadeMatrix(light, corners, 2048);

    // All frustum corners should project inside the cascade's NDC [-1,1]
    for (const auto& corner : corners)
    {
        glm::vec4 clip = cascadeMatrix * glm::vec4(corner, 1.0f);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;

        // Allow a small margin for texel snapping
        EXPECT_GE(ndc.x, -1.1f) << "Corner outside left: " << ndc.x;
        EXPECT_LE(ndc.x,  1.1f) << "Corner outside right: " << ndc.x;
        EXPECT_GE(ndc.y, -1.1f) << "Corner outside bottom: " << ndc.y;
        EXPECT_LE(ndc.y,  1.1f) << "Corner outside top: " << ndc.y;
    }
}

TEST(CascadedShadowMapTest, CascadeMatrixIsInvertible)
{
    DirectionalLight light;
    light.direction = glm::vec3(0.0f, -1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 10.0f);

    auto corners = CascadedShadowMap::computeFrustumCorners(proj * view);
    glm::mat4 cascadeMatrix = CascadedShadowMap::computeCascadeMatrix(light, corners, 2048);

    float det = glm::determinant(cascadeMatrix);
    EXPECT_NE(det, 0.0f);
}

TEST(CascadedShadowMapTest, CloserCascadeHasHigherTexelDensity)
{
    DirectionalLight light;
    light.direction = glm::vec3(-0.2f, -1.0f, -0.3f);

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 5.0f, 10.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    // Near cascade: 0.1 to 5
    glm::mat4 nearProj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 5.0f);
    auto nearCorners = CascadedShadowMap::computeFrustumCorners(nearProj * view);
    glm::mat4 nearMatrix = CascadedShadowMap::computeCascadeMatrix(light, nearCorners, 2048);

    // Far cascade: 50 to 150
    glm::mat4 farProj = glm::perspective(glm::radians(45.0f), 1.0f, 50.0f, 150.0f);
    auto farCorners = CascadedShadowMap::computeFrustumCorners(farProj * view);
    glm::mat4 farMatrix = CascadedShadowMap::computeCascadeMatrix(light, farCorners, 2048);

    // Near cascade should have larger scale (more texels per world unit)
    glm::vec4 testDir(1.0f, 0.0f, 0.0f, 0.0f);
    float nearScale = glm::length(glm::vec3(nearMatrix * testDir));
    float farScale = glm::length(glm::vec3(farMatrix * testDir));

    EXPECT_GT(nearScale, farScale);
}

// 3D_E-0874 lane-04: the texel-grid snap of the origin only holds still if
// the texel size does too. A camera turning in place gives the same frustum
// shape every frame, so the cascade's width must not move by float noise.
TEST(CascadedShadowMapTest, TurningCameraKeepsTheCascadeWidthExactly)
{
    DirectionalLight light;
    // Straight down: the light's rotation is then exact (0 and +-1 only), so
    // the matrix scale below is the cascade width and nothing else.
    light.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 12.0f);
    const glm::vec3 eye(3.0f, 1.7f, -4.0f);

    float firstScale = 0.0f;
    for (int step = 0; step < 36; ++step)
    {
        const float yaw = glm::radians(10.0f * static_cast<float>(step) + 0.37f);
        const glm::vec3 forward(std::cos(yaw), -0.1f, std::sin(yaw));
        glm::mat4 view = glm::lookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
        auto corners = CascadedShadowMap::computeFrustumCorners(proj * view);
        glm::mat4 m = CascadedShadowMap::computeCascadeMatrix(light, corners, 2048);
        // Row 0 of the 3x3 is the ortho x scale (1 / radius) times a row of
        // the light's rotation, so its length is 1 / radius.
        const float scale = glm::length(glm::vec3(m[0][0], m[1][0], m[2][0]));
        if (step == 0)
        {
            firstScale = scale;
        }
        EXPECT_EQ(scale, firstScale) << "yaw step " << step;
    }
}

TEST(CascadedShadowMapTest, VerticalLightDirectionDoesNotDegenerate)
{
    DirectionalLight light;
    light.direction = glm::vec3(0.0f, -1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 5.0f, 10.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 20.0f);

    auto corners = CascadedShadowMap::computeFrustumCorners(proj * view);
    glm::mat4 cascadeMatrix = CascadedShadowMap::computeCascadeMatrix(light, corners, 2048);

    float det = glm::determinant(cascadeMatrix);
    EXPECT_NE(det, 0.0f);
}

// =============================================================================
// Split boundary tests
// =============================================================================

// Slice 18 Ts4: dropped `SplitsAdjacentCascadesAreContiguous` — strict
// monotonic increase is already pinned by
// `SplitDistancesMonotonicallyIncreasing` above, and the range pin
// (`SplitDistancesWithinRange`) together with monotonic-increase
// implies "contiguous adjacent splits". All three would flip together.

TEST(CascadedShadowMapTest, LambdaBlendsBetweenLinearAndLog)
{
    float near = 0.1f;
    float far = 150.0f;
    auto logSplits = CascadedShadowMap::computeSplitDistances(near, far, 4, 1.0f);
    auto linSplits = CascadedShadowMap::computeSplitDistances(near, far, 4, 0.0f);
    auto blendSplits = CascadedShadowMap::computeSplitDistances(near, far, 4, 0.5f);

    // Blended splits should be between linear and logarithmic (for first split)
    float minFirst = std::min(logSplits[0], linSplits[0]);
    float maxFirst = std::max(logSplits[0], linSplits[0]);
    EXPECT_GE(blendSplits[0], minFirst - 0.001f);
    EXPECT_LE(blendSplits[0], maxFirst + 0.001f);
}

// =============================================================================
// Bake shadow volume (3D_E-0731)
// =============================================================================

// A probe bake renders from positions all over the scene, so its one shadow
// volume must contain the whole box it is fitted to: a corner outside the
// volume is a receiver the bake treats as unshadowed.
TEST(CascadedShadowMapTest, BoundsMatrixContainsEveryCornerOfTheBox)
{
    DirectionalLight light;
    light.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, 0.3f));

    AABB box;                                  // a long, low courtyard
    box.min = glm::vec3(-15.0f, -0.5f, -40.0f);
    box.max = glm::vec3(15.0f, 8.0f, 20.0f);

    const glm::mat4 m = CascadedShadowMap::computeBoundsMatrix(light, box, 2048);

    for (int i = 0; i < 8; i++)
    {
        const glm::vec3 corner((i & 1) ? box.max.x : box.min.x,
                               (i & 2) ? box.max.y : box.min.y,
                               (i & 4) ? box.max.z : box.min.z);
        const glm::vec4 clip = m * glm::vec4(corner, 1.0f);
        EXPECT_GE(clip.x, -1.0f) << "corner " << i;
        EXPECT_LE(clip.x, 1.0f) << "corner " << i;
        EXPECT_GE(clip.y, -1.0f) << "corner " << i;
        EXPECT_LE(clip.y, 1.0f) << "corner " << i;
        EXPECT_GE(clip.z, -1.0f) << "corner " << i;
        EXPECT_LE(clip.z, 1.0f) << "corner " << i;
    }

    // And it is fitted, not merely huge: a point three box-lengths away is outside.
    const glm::vec4 far = m * glm::vec4(0.0f, 0.0f, 250.0f, 1.0f);
    EXPECT_TRUE(std::abs(far.x) > 1.0f || std::abs(far.y) > 1.0f || std::abs(far.z) > 1.0f);
}

// =============================================================================
// Staggered cascade rebuilds (3D_E-0029) and the casters-per-cascade statistic
// =============================================================================

TEST(CascadedShadowMapTest, NearCascadesRebuildEveryFrameFarOnesInTurn)
{
    // After the first four frames, cascade 2 rebuilds on frames 0 mod 4 and
    // cascade 3 on frames 2 mod 4; cascades 0 and 1 rebuild every frame.
    for (uint32_t frame = 5; frame <= 12; ++frame)
    {
        EXPECT_TRUE(CascadedShadowMap::rebuildsOnFrame(0, 4, frame, false));
        EXPECT_TRUE(CascadedShadowMap::rebuildsOnFrame(1, 4, frame, false));
        EXPECT_EQ(CascadedShadowMap::rebuildsOnFrame(2, 4, frame, false), frame % 4 == 0);
        EXPECT_EQ(CascadedShadowMap::rebuildsOnFrame(3, 4, frame, false), frame % 4 == 2);
    }
}

TEST(CascadedShadowMapTest, EveryCascadeRebuildsOnTheFirstFrames)
{
    for (uint32_t frame = 1; frame <= 4; ++frame)
    {
        for (int c = 0; c < 4; ++c)
        {
            EXPECT_TRUE(CascadedShadowMap::rebuildsOnFrame(c, 4, frame, false));
        }
    }
}

TEST(CascadedShadowMapTest, BakeRebuildsCascadeZeroOnly)
{
    EXPECT_TRUE(CascadedShadowMap::rebuildsOnFrame(0, 4, 8, true));
    for (int c = 1; c < 4; ++c)
    {
        EXPECT_FALSE(CascadedShadowMap::rebuildsOnFrame(c, 4, 8, true));
    }
}

TEST(CascadedShadowMapTest, CastersPerCascadeCountsOnlyRebuiltCascades)
{
    // Frame 6: cascades 0, 1 and 3 drew 30 casters between them; 2 skipped.
    EXPECT_EQ(CascadedShadowMap::castersPerRebuiltCascade(30, 4, 6, false), 10);
    // Frame 7: only cascades 0 and 1 drew.
    EXPECT_EQ(CascadedShadowMap::castersPerRebuiltCascade(30, 4, 7, false), 15);
    // First frames: all four drew.
    EXPECT_EQ(CascadedShadowMap::castersPerRebuiltCascade(40, 4, 1, false), 10);
    // A bake draws cascade 0 only.
    EXPECT_EQ(CascadedShadowMap::castersPerRebuiltCascade(12, 4, 0, true), 12);
    EXPECT_EQ(CascadedShadowMap::castersPerRebuiltCascade(12, 0, 6, false), 0);
}
