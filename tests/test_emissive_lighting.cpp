// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_emissive_lighting.cpp
/// @brief Tests for emissive material lighting and EmissiveLightComponent.
#include <gtest/gtest.h>
#include "scene/light_component.h"

using namespace Vestige;

// Phase 10.9 Slice 18 Ts3 consolidation: the Material emissive
// property tests (default-black / default-strength-one /
// SetGetEmissive* / EmissiveStrengthClamped*) lived in both this file
// and `test_pbr_material.cpp`. Canonical home is the PBR file —
// emissive is a Material PBR field, not an EmissiveLightComponent
// concern. The HDR-product / zero-strength tests are also dropped
// per Ts1 above (Material*Float arithmetic, no SUT call).

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


// Slice 18 Ts1 cleanup: dropped `PointLightCountRespected` — a
// tautology on a compile-time constant (`EXPECT_EQ(MAX_POINT_LIGHTS, 8)`).
// If the constant changes, this test would need updating in lockstep
// regardless — it pins the literal, not the cap behavior. The
// observable cap is pinned by scene-collect tests when the scene has
// > N emissive lights.
