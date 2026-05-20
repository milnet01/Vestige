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
// PBR scalar field clamping
//
// Every clamped scalar setter shares one contract: inputs below the floor
// clamp up, inputs above the ceiling clamp down, in-range inputs round-trip
// unchanged. The fields differ only in their bounds and accessor pair, so
// they collapse into one TEST_P clamp table. Non-capturing lambdas convert
// to the `set`/`get` function pointers.
// =============================================================================
namespace
{
struct ClampCase
{
    const char* name;
    void  (*set)(Material&, float);
    float (*get)(const Material&);
    float belowInput;  float clampedLow;
    float aboveInput;  float clampedHigh;
    float midInput;    float midExpect;
};

class PbrScalarClamp : public ::testing::TestWithParam<ClampCase> {};
} // namespace

TEST_P(PbrScalarClamp, ClampsBelowFloorAboveCeilingAndRoundTrips)
{
    const ClampCase& c = GetParam();
    Material mat;

    c.set(mat, c.belowInput);
    EXPECT_FLOAT_EQ(c.get(mat), c.clampedLow)  << c.name << " below-floor";

    c.set(mat, c.aboveInput);
    EXPECT_FLOAT_EQ(c.get(mat), c.clampedHigh) << c.name << " above-ceiling";

    c.set(mat, c.midInput);
    EXPECT_FLOAT_EQ(c.get(mat), c.midExpect)   << c.name << " in-range round-trip";
}

INSTANTIATE_TEST_SUITE_P(
    AllClampedScalars,
    PbrScalarClamp,
    ::testing::Values(
        ClampCase{"Metallic",
            [](Material& m, float v){ m.setMetallic(v); },
            [](const Material& m){ return m.getMetallic(); },
            -0.5f, 0.0f,  1.5f, 1.0f,  0.7f, 0.7f},
        // Roughness floor is 0.04 (avoids GGX singularity), ceiling 1.0.
        ClampCase{"Roughness",
            [](Material& m, float v){ m.setRoughness(v); },
            [](const Material& m){ return m.getRoughness(); },
            0.0f, 0.04f,  2.0f, 1.0f,  0.5f, 0.5f},
        ClampCase{"Ao",
            [](Material& m, float v){ m.setAo(v); },
            [](const Material& m){ return m.getAo(); },
            -0.1f, 0.0f,  1.5f, 1.0f,  0.5f, 0.5f},
        // Emissive-strength ceiling is 100.0.
        ClampCase{"EmissiveStrength",
            [](Material& m, float v){ m.setEmissiveStrength(v); },
            [](const Material& m){ return m.getEmissiveStrength(); },
            -1.0f, 0.0f,  200.0f, 100.0f,  5.0f, 5.0f},
        ClampCase{"Clearcoat",
            [](Material& m, float v){ m.setClearcoat(v); },
            [](const Material& m){ return m.getClearcoat(); },
            -0.5f, 0.0f,  1.7f, 1.0f,  0.5f, 0.5f},
        ClampCase{"ClearcoatRoughness",
            [](Material& m, float v){ m.setClearcoatRoughness(v); },
            [](const Material& m){ return m.getClearcoatRoughness(); },
            -0.5f, 0.0f,  2.0f, 1.0f,  0.25f, 0.25f}),
    [](const ::testing::TestParamInfo<ClampCase>& info)
    { return std::string(info.param.name); });

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

TEST(PbrMaterialTest, ClearcoatDefaults)
{
    Material mat;
    // Default clearcoat off, but clearcoat-roughness ships at 0.04 to
    // match the base-PBR roughness floor (GGX numerical stability).
    // Clamp + round-trip behaviour lives in the PbrScalarClamp table above.
    EXPECT_FLOAT_EQ(mat.getClearcoat(), 0.0f);
    EXPECT_FLOAT_EQ(mat.getClearcoatRoughness(), 0.04f);
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
    // /test-audit 2026-05-17 Ts19-CG4: the prior body only EXPECT_NO_THROW'd
    // on the setter — passes even if the setter is a stub. With getAlphaCutoff
    // present on Material, a numeric round-trip is the real contract.
    mat.setAlphaCutoff(0.25f);
    EXPECT_FLOAT_EQ(mat.getAlphaCutoff(), 0.25f);
    mat.setAlphaCutoff(0.75f);
    EXPECT_FLOAT_EQ(mat.getAlphaCutoff(), 0.75f);
    mat.setAlphaCutoff(0.0f);
    EXPECT_FLOAT_EQ(mat.getAlphaCutoff(), 0.0f);
    mat.setAlphaCutoff(1.0f);
    EXPECT_FLOAT_EQ(mat.getAlphaCutoff(), 1.0f);
}

TEST(PbrMaterialTest, DoubleSidedToggles)
{
    Material mat;
    // Default not double-sided — gltf-spec opt-in field.
    EXPECT_FALSE(mat.isDoubleSided());
    mat.setDoubleSided(true);
    EXPECT_TRUE(mat.isDoubleSided());
    mat.setDoubleSided(false);
    EXPECT_FALSE(mat.isDoubleSided());
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
