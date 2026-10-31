// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_light_color.cpp
/// @brief A light's stored colour splits into colour + brightness and back,
///        and a lamp made in the editor is bright enough to see (3D_E-0765).
#include <gtest/gtest.h>

#include "editor/entity_factory.h"
#include "editor/light_color.h"
#include "scene/entity.h"
#include "scene/light_component.h"
#include "scene/scene.h"

#include <algorithm>

using namespace Vestige;

TEST(LightColor, SplitTakesBrightnessFromTheBrightestChannel)
{
    // The Tabernacle altar fire's stored value: brighter than 1.0.
    const LightColor lc = splitLightColor(glm::vec3(2.5f, 1.4f, 0.4f));
    EXPECT_FLOAT_EQ(lc.brightness, 2.5f);
    EXPECT_FLOAT_EQ(lc.color.r, 1.0f);
    EXPECT_FLOAT_EQ(lc.color.g, 0.56f);
    EXPECT_FLOAT_EQ(lc.color.b, 0.16f);
}

TEST(LightColor, CombineUndoesSplit)
{
    const glm::vec3 stored(0.3f, 0.5f, 0.9f);
    const glm::vec3 back = combineLightColor(splitLightColor(stored));
    EXPECT_NEAR(back.r, stored.r, 1e-6f);
    EXPECT_NEAR(back.g, stored.g, 1e-6f);
    EXPECT_NEAR(back.b, stored.b, 1e-6f);
}

TEST(LightColor, BlackSplitsToWhiteAtZeroBrightness)
{
    // A switched-off light keeps a usable colour, so raising the
    // brightness slider again gives light rather than more black.
    const LightColor lc = splitLightColor(glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(lc.brightness, 0.0f);
    EXPECT_EQ(lc.color, glm::vec3(1.0f));
}

TEST(LightColor, ReconcileKeepsAPickedColourThatStillMatches)
{
    LightColor picked;
    picked.color = glm::vec3(0.5f);
    picked.brightness = 3.0f;
    const LightColor kept = reconcileLightColor(picked, combineLightColor(picked));
    EXPECT_EQ(kept.color, glm::vec3(0.5f));
    EXPECT_FLOAT_EQ(kept.brightness, 3.0f);
}

TEST(LightColor, ReconcileResplitsWhenTheLightChangedElsewhere)
{
    // A script or undo changed the light; the inspector must follow it.
    LightColor picked;
    const LightColor fresh = reconcileLightColor(picked, glm::vec3(0.0f, 2.0f, 0.0f));
    EXPECT_EQ(fresh.color, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FLOAT_EQ(fresh.brightness, 2.0f);
}

TEST(LightColor, NewEditorPointLightOutshinesTheMeadowSunAtTwoMetres)
{
    // 3D_E-0765: a lamp added from Create > Lights lit nothing visible in
    // daylight. The meadow sun's diffuse is 3.61 (Engine::setupDemoScene);
    // a lamp 2 m from a surface should reach at least that.
    Scene scene("LightColorTest");
    Entity* e = EntityFactory::createPointLight(scene, glm::vec3(0.0f));
    ASSERT_NE(e, nullptr);
    const auto* comp = e->getComponent<PointLightComponent>();
    ASSERT_NE(comp, nullptr);

    const PointLight& pl = comp->light;
    const float d = 2.0f;
    const float attenuation = 1.0f / (pl.constant + pl.linear * d + pl.quadratic * d * d);
    const float brightest = std::max({pl.diffuse.r, pl.diffuse.g, pl.diffuse.b});
    EXPECT_GE(brightest * attenuation, 3.61f);
}
