/// @file test_emissive_lighting.cpp
/// @brief Tests for emissive material lighting and EmissiveLightComponent.
#include <gtest/gtest.h>
#include "renderer/material.h"
#include "renderer/light.h"
#include "scene/light_component.h"

using namespace Vestige;

// =============================================================================
// Material emissive properties
// =============================================================================

TEST(EmissiveLighting, DefaultEmissiveIsBlack)
{
    Material mat;
    EXPECT_EQ(mat.getEmissive(), glm::vec3(0.0f));
}

TEST(EmissiveLighting, DefaultEmissiveStrengthIsOne)
{
    Material mat;
    EXPECT_FLOAT_EQ(mat.getEmissiveStrength(), 1.0f);
}

TEST(EmissiveLighting, SetGetEmissiveColor)
{
    Material mat;
    glm::vec3 color(1.0f, 0.3f, 0.05f);
    mat.setEmissive(color);
    EXPECT_EQ(mat.getEmissive(), color);
}

TEST(EmissiveLighting, SetGetEmissiveStrength)
{
    Material mat;
    mat.setEmissiveStrength(5.0f);
    EXPECT_FLOAT_EQ(mat.getEmissiveStrength(), 5.0f);
}

TEST(EmissiveLighting, EmissiveStrengthClampedLow)
{
    Material mat;
    mat.setEmissiveStrength(-1.0f);
    EXPECT_FLOAT_EQ(mat.getEmissiveStrength(), 0.0f);
}

TEST(EmissiveLighting, EmissiveStrengthClampedHigh)
{
    Material mat;
    mat.setEmissiveStrength(200.0f);
    EXPECT_FLOAT_EQ(mat.getEmissiveStrength(), 100.0f);
}

TEST(EmissiveLighting, EmissiveColorTimesStrengthProducesHDR)
{
    Material mat;
    mat.setEmissive(glm::vec3(1.0f, 0.5f, 0.0f));
    mat.setEmissiveStrength(5.0f);

    glm::vec3 hdrEmissive = mat.getEmissive() * mat.getEmissiveStrength();
    // At least one channel > 1.0 means HDR
    EXPECT_GT(hdrEmissive.r, 1.0f);
}

TEST(EmissiveLighting, ZeroStrengthMeansNoEmission)
{
    Material mat;
    mat.setEmissive(glm::vec3(1.0f, 1.0f, 1.0f));
    mat.setEmissiveStrength(0.0f);

    glm::vec3 result = mat.getEmissive() * mat.getEmissiveStrength();
    EXPECT_EQ(result, glm::vec3(0.0f));
}

// =============================================================================
// EmissiveLightComponent
// =============================================================================

TEST(EmissiveLighting, ComponentDefaultValues)
{
    EmissiveLightComponent comp;
    EXPECT_FLOAT_EQ(comp.lightRadius, 5.0f);
    EXPECT_FLOAT_EQ(comp.lightIntensity, 1.0f);
    EXPECT_EQ(comp.overrideColor, glm::vec3(0.0f));
}

TEST(EmissiveLighting, ComponentOverrideColor)
{
    EmissiveLightComponent comp;
    comp.overrideColor = glm::vec3(0.0f, 1.0f, 0.0f);
    EXPECT_EQ(comp.overrideColor, glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(EmissiveLighting, AttenuationFromRadius)
{
    // Verify the attenuation formula produces sensible values
    float radius = 5.0f;
    float linear = 2.0f / radius;
    float quadratic = 1.0f / (radius * radius);

    // At distance == radius, attenuation should be small but nonzero
    float attenuation = 1.0f / (1.0f + linear * radius + quadratic * radius * radius);
    EXPECT_GT(attenuation, 0.0f);
    EXPECT_LT(attenuation, 0.5f);  // Should be significantly dimmed at radius distance
}

TEST(EmissiveLighting, PointLightCountRespected)
{
    // Verify MAX_POINT_LIGHTS is 8 (test depends on this constant)
    EXPECT_EQ(MAX_POINT_LIGHTS, 8);
}
