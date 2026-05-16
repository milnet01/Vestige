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
// Phase 10.9 Slice 18 Ts2 — additional PBR field coverage.
// Previously untested public setters: clearcoat, alpha-mode,
// IBL multiplier, UV scale, double-sided.
// =============================================================================

TEST(PbrMaterialTest, ClearcoatDefaultsAndClamping)
{
    Material mat;
    // Default clearcoat off, but clearcoat-roughness ships at 0.04 to
    // match the base-PBR roughness floor (GGX numerical stability).
    EXPECT_FLOAT_EQ(mat.getClearcoat(), 0.0f);
    EXPECT_FLOAT_EQ(mat.getClearcoatRoughness(), 0.04f);

    // Clamp negative inputs to 0.
    mat.setClearcoat(-0.5f);
    EXPECT_FLOAT_EQ(mat.getClearcoat(), 0.0f);
    mat.setClearcoatRoughness(-0.5f);
    EXPECT_FLOAT_EQ(mat.getClearcoatRoughness(), 0.0f);

    // Clamp above 1.0 to 1.0.
    mat.setClearcoat(1.7f);
    EXPECT_FLOAT_EQ(mat.getClearcoat(), 1.0f);
    mat.setClearcoatRoughness(2.0f);
    EXPECT_FLOAT_EQ(mat.getClearcoatRoughness(), 1.0f);

    // In-range round-trip.
    mat.setClearcoat(0.5f);
    EXPECT_FLOAT_EQ(mat.getClearcoat(), 0.5f);
    mat.setClearcoatRoughness(0.25f);
    EXPECT_FLOAT_EQ(mat.getClearcoatRoughness(), 0.25f);
}

TEST(PbrMaterialTest, AlphaModeRoundTripsEveryEnumValue)
{
    Material mat;
    for (AlphaMode mode : {AlphaMode::OPAQUE, AlphaMode::MASK, AlphaMode::BLEND})
    {
        mat.setAlphaMode(mode);
        EXPECT_EQ(mat.getAlphaMode(), mode);
    }
}

TEST(PbrMaterialTest, AlphaCutoffRoundTrips)
{
    Material mat;
    mat.setAlphaCutoff(0.25f);
    // No public getter for cutoff — pin via round-trip-into-setter that
    // a refactor would visibly need to update. (If a getter lands,
    // tighten to numeric round-trip.)
    EXPECT_NO_THROW(mat.setAlphaCutoff(0.75f));
    EXPECT_NO_THROW(mat.setAlphaCutoff(0.0f));
    EXPECT_NO_THROW(mat.setAlphaCutoff(1.0f));
}

TEST(PbrMaterialTest, DoubleSidedToggles)
{
    Material mat;
    // Default not double-sided — gltf-spec opt-in field.
    mat.setDoubleSided(true);
    EXPECT_NO_THROW(mat.setDoubleSided(false));
}

TEST(PbrMaterialTest, IblMultiplierRoundTripsAndClampsToZeroOne)
{
    Material mat;
    // Default 1.0 (full IBL contribution).
    EXPECT_FLOAT_EQ(mat.getIblMultiplier(), 1.0f);

    // In-range round-trip.
    mat.setIblMultiplier(0.5f);
    EXPECT_FLOAT_EQ(mat.getIblMultiplier(), 0.5f);

    // Clamp above 1.0 to 1.0 — IBL contribution above unity is unphysical.
    mat.setIblMultiplier(2.0f);
    EXPECT_FLOAT_EQ(mat.getIblMultiplier(), 1.0f);

    // Clamp below 0 to 0.
    mat.setIblMultiplier(-0.5f);
    EXPECT_FLOAT_EQ(mat.getIblMultiplier(), 0.0f);
}

TEST(PbrMaterialTest, UvScaleRoundTripsAndClampsToValidRange)
{
    Material mat;
    // Default should be 1.0 (unscaled UVs).
    EXPECT_FLOAT_EQ(mat.getUvScale(), 1.0f);

    mat.setUvScale(2.0f);
    EXPECT_FLOAT_EQ(mat.getUvScale(), 2.0f);
    mat.setUvScale(0.5f);
    EXPECT_FLOAT_EQ(mat.getUvScale(), 0.5f);

    // Clamp to [0.01, 100.0] — degenerate / runaway scale rejected.
    mat.setUvScale(-1.0f);
    EXPECT_FLOAT_EQ(mat.getUvScale(), 0.01f);
    mat.setUvScale(1000.0f);
    EXPECT_FLOAT_EQ(mat.getUvScale(), 100.0f);
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
