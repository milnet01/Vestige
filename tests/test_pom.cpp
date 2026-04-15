// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_pom.cpp
/// @brief Unit tests for parallax occlusion mapping — material state and algorithm math.
#include "renderer/material.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

using namespace Vestige;

// ============================================================================
// MaterialPOMTest — Material class POM state
// ============================================================================

TEST(MaterialPOMTest, DefaultHeightScale)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.getHeightScale(), 0.05f);
}

TEST(MaterialPOMTest, DefaultPomEnabled)
{
    Material mat;
    EXPECT_TRUE(mat.isPomEnabled());
}

TEST(MaterialPOMTest, HeightMapNullByDefault)
{
    Material mat;
    EXPECT_EQ(mat.getHeightMap(), nullptr);
    EXPECT_FALSE(mat.hasHeightMap());
}

TEST(MaterialPOMTest, HeightScaleClamping)
{
    Material mat;

    // Below minimum
    mat.setHeightScale(-0.5f);
    EXPECT_FLOAT_EQ(mat.getHeightScale(), 0.0f);

    // Above maximum
    mat.setHeightScale(0.5f);
    EXPECT_FLOAT_EQ(mat.getHeightScale(), 0.2f);

    // Within range
    mat.setHeightScale(0.1f);
    EXPECT_FLOAT_EQ(mat.getHeightScale(), 0.1f);

    // Exact boundaries
    mat.setHeightScale(0.0f);
    EXPECT_FLOAT_EQ(mat.getHeightScale(), 0.0f);

    mat.setHeightScale(0.2f);
    EXPECT_FLOAT_EQ(mat.getHeightScale(), 0.2f);
}

TEST(MaterialPOMTest, PomEnableToggle)
{
    Material mat;

    mat.setPomEnabled(false);
    EXPECT_FALSE(mat.isPomEnabled());

    mat.setPomEnabled(true);
    EXPECT_TRUE(mat.isPomEnabled());
}

// ============================================================================
// POMAlgorithmTest — Shader math verification (C++ mirrors)
// ============================================================================

/// Mirrors the adaptive layer count calculation from the GLSL shader.
static float calcLayerCount(const glm::vec3& viewDirTS)
{
    return glm::mix(32.0f, 8.0f, std::abs(viewDirTS.z));
}

TEST(POMAlgorithmTest, LayerCountStraightDown)
{
    // Looking straight down: viewDirTS.z = 1.0 → should use minimum layers (8)
    glm::vec3 viewDirTS(0.0f, 0.0f, 1.0f);
    float layers = calcLayerCount(viewDirTS);
    EXPECT_FLOAT_EQ(layers, 8.0f);
}

TEST(POMAlgorithmTest, LayerCountGrazingAngle)
{
    // Looking at a grazing angle: viewDirTS.z ≈ 0.0 → should use maximum layers (32)
    glm::vec3 viewDirTS(1.0f, 0.0f, 0.0f);
    viewDirTS = glm::normalize(viewDirTS);
    float layers = calcLayerCount(viewDirTS);
    EXPECT_FLOAT_EQ(layers, 32.0f);
}

TEST(POMAlgorithmTest, UVOffsetScalesWithHeightScale)
{
    // The UV shift direction (p = viewDirTS.xy * heightScale) should scale linearly
    glm::vec3 viewDirTS = glm::normalize(glm::vec3(0.5f, 0.3f, 0.8f));

    float scale1 = 0.05f;
    float scale2 = 0.10f;

    glm::vec2 p1 = glm::vec2(viewDirTS.x, viewDirTS.y) * scale1;
    glm::vec2 p2 = glm::vec2(viewDirTS.x, viewDirTS.y) * scale2;

    // p2 should be exactly 2x p1
    EXPECT_NEAR(p2.x, p1.x * 2.0f, 1e-6f);
    EXPECT_NEAR(p2.y, p1.y * 2.0f, 1e-6f);
}

TEST(POMAlgorithmTest, ZeroUVOffsetWhenStraightDown)
{
    // When viewing straight down, viewDirTS.xy = (0,0) → no UV offset regardless of scale
    glm::vec3 viewDirTS(0.0f, 0.0f, 1.0f);
    float heightScale = 0.1f;

    glm::vec2 p = glm::vec2(viewDirTS.x, viewDirTS.y) * heightScale;

    EXPECT_FLOAT_EQ(p.x, 0.0f);
    EXPECT_FLOAT_EQ(p.y, 0.0f);
}
