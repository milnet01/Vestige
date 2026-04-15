// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_pbr_material.cpp
/// @brief Tests for PBR material properties and MaterialType switching.
#include <gtest/gtest.h>

#include "renderer/material.h"

using namespace Vestige;

// =============================================================================
// MaterialType tests
// =============================================================================

TEST(PbrMaterialTest, DefaultTypeIsBlinnPhong)
{
    Material mat;
    EXPECT_EQ(mat.getType(), MaterialType::BLINN_PHONG);
}

TEST(PbrMaterialTest, CanSwitchToPBR)
{
    Material mat;
    mat.setType(MaterialType::PBR);
    EXPECT_EQ(mat.getType(), MaterialType::PBR);
}

TEST(PbrMaterialTest, CanSwitchBackToBlinnPhong)
{
    Material mat;
    mat.setType(MaterialType::PBR);
    mat.setType(MaterialType::BLINN_PHONG);
    EXPECT_EQ(mat.getType(), MaterialType::BLINN_PHONG);
}

// =============================================================================
// PBR field defaults
// =============================================================================

TEST(PbrMaterialTest, DefaultAlbedoIsGrey)
{
    Material mat;
    glm::vec3 albedo = mat.getAlbedo();
    EXPECT_FLOAT_EQ(albedo.r, 0.8f);
    EXPECT_FLOAT_EQ(albedo.g, 0.8f);
    EXPECT_FLOAT_EQ(albedo.b, 0.8f);
}

TEST(PbrMaterialTest, DefaultMetallicIsZero)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.getMetallic(), 0.0f);
}

TEST(PbrMaterialTest, DefaultRoughnessIsHalf)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.getRoughness(), 0.5f);
}

TEST(PbrMaterialTest, DefaultAoIsOne)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.getAo(), 1.0f);
}

TEST(PbrMaterialTest, DefaultEmissiveIsBlack)
{
    Material mat;
    glm::vec3 emissive = mat.getEmissive();
    EXPECT_FLOAT_EQ(emissive.r, 0.0f);
    EXPECT_FLOAT_EQ(emissive.g, 0.0f);
    EXPECT_FLOAT_EQ(emissive.b, 0.0f);
}

TEST(PbrMaterialTest, DefaultEmissiveStrengthIsOne)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.getEmissiveStrength(), 1.0f);
}

// =============================================================================
// PBR field clamping
// =============================================================================

TEST(PbrMaterialTest, MetallicClampedToZeroOne)
{
    Material mat;

    mat.setMetallic(-0.5f);
    EXPECT_FLOAT_EQ(mat.getMetallic(), 0.0f);

    mat.setMetallic(1.5f);
    EXPECT_FLOAT_EQ(mat.getMetallic(), 1.0f);

    mat.setMetallic(0.7f);
    EXPECT_FLOAT_EQ(mat.getMetallic(), 0.7f);
}

TEST(PbrMaterialTest, RoughnessClampedWithMinimum)
{
    Material mat;

    // Minimum roughness is 0.04 to avoid GGX singularity
    mat.setRoughness(0.0f);
    EXPECT_FLOAT_EQ(mat.getRoughness(), 0.04f);

    mat.setRoughness(0.01f);
    EXPECT_FLOAT_EQ(mat.getRoughness(), 0.04f);

    mat.setRoughness(2.0f);
    EXPECT_FLOAT_EQ(mat.getRoughness(), 1.0f);

    mat.setRoughness(0.5f);
    EXPECT_FLOAT_EQ(mat.getRoughness(), 0.5f);
}

TEST(PbrMaterialTest, AoClampedToZeroOne)
{
    Material mat;

    mat.setAo(-0.1f);
    EXPECT_FLOAT_EQ(mat.getAo(), 0.0f);

    mat.setAo(1.5f);
    EXPECT_FLOAT_EQ(mat.getAo(), 1.0f);
}

TEST(PbrMaterialTest, EmissiveStrengthClamped)
{
    Material mat;

    mat.setEmissiveStrength(-1.0f);
    EXPECT_FLOAT_EQ(mat.getEmissiveStrength(), 0.0f);

    mat.setEmissiveStrength(200.0f);
    EXPECT_FLOAT_EQ(mat.getEmissiveStrength(), 100.0f);

    mat.setEmissiveStrength(5.0f);
    EXPECT_FLOAT_EQ(mat.getEmissiveStrength(), 5.0f);
}

// =============================================================================
// PBR texture slots
// =============================================================================

TEST(PbrMaterialTest, NoTexturesByDefault)
{
    Material mat;
    EXPECT_FALSE(mat.hasMetallicRoughnessTexture());
    EXPECT_FALSE(mat.hasEmissiveTexture());
    EXPECT_FALSE(mat.hasAoTexture());
    EXPECT_EQ(mat.getMetallicRoughnessTexture(), nullptr);
    EXPECT_EQ(mat.getEmissiveTexture(), nullptr);
    EXPECT_EQ(mat.getAoTexture(), nullptr);
}

// =============================================================================
// PBR set/get round-trip
// =============================================================================

TEST(PbrMaterialTest, AlbedoSetGet)
{
    Material mat;
    mat.setAlbedo(glm::vec3(0.2f, 0.4f, 0.6f));
    glm::vec3 albedo = mat.getAlbedo();
    EXPECT_FLOAT_EQ(albedo.r, 0.2f);
    EXPECT_FLOAT_EQ(albedo.g, 0.4f);
    EXPECT_FLOAT_EQ(albedo.b, 0.6f);
}

TEST(PbrMaterialTest, EmissiveSetGet)
{
    Material mat;
    mat.setEmissive(glm::vec3(1.0f, 0.5f, 0.0f));
    glm::vec3 emissive = mat.getEmissive();
    EXPECT_FLOAT_EQ(emissive.r, 1.0f);
    EXPECT_FLOAT_EQ(emissive.g, 0.5f);
    EXPECT_FLOAT_EQ(emissive.b, 0.0f);
}

// =============================================================================
// Blinn-Phong properties unchanged
// =============================================================================

TEST(PbrMaterialTest, BlinnPhongDefaultsUnchanged)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.getShininess(), 32.0f);
    glm::vec3 diffuse = mat.getDiffuseColor();
    EXPECT_FLOAT_EQ(diffuse.r, 0.8f);
    glm::vec3 specular = mat.getSpecularColor();
    EXPECT_FLOAT_EQ(specular.r, 1.0f);
}
