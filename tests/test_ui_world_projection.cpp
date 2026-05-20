// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_world_projection.cpp
/// @brief Tests for the world-to-screen projection helper used by in-world UI.

#include <gtest/gtest.h>

#include "ui/ui_world_projection.h"
#include "ui/ui_interaction_prompt.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Vestige;

namespace
{

// Build a simple view+projection: camera at origin looking down -Z, 60° FOV.
glm::mat4 makeViewProj(float aspect = 16.0f / 9.0f)
{
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),
                                        glm::vec3(0.0f, 0.0f, -1.0f),
                                        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect,
                                              0.1f, 100.0f);
    return proj * view;
}

} // namespace

// -- Frustum culling --

TEST(UIWorldProjection, BehindCameraIsInvisible)
{
    const auto vp = makeViewProj();
    // Camera looks down -Z; a point at +Z is behind it.
    const auto r = projectWorldToScreen(glm::vec3(0, 0, 5), vp, 1920, 1080);
    EXPECT_FALSE(r.visible);
}

TEST(UIWorldProjection, PointDirectlyAheadIsCentred)
{
    const auto vp = makeViewProj();
    const auto r = projectWorldToScreen(glm::vec3(0, 0, -5), vp, 1920, 1080);
    ASSERT_TRUE(r.visible);
    EXPECT_NEAR(r.screenPos.x, 960.0f, 1.0f);
    EXPECT_NEAR(r.screenPos.y, 540.0f, 1.0f);
}

TEST(UIWorldProjection, OffscreenLeftIsCulled)
{
    const auto vp = makeViewProj();
    // Far off to the side at modest depth — should fall outside frustum.
    const auto r = projectWorldToScreen(glm::vec3(-50, 0, -1), vp, 1920, 1080);
    EXPECT_FALSE(r.visible);
}

TEST(UIWorldProjection, NdcDepthIsInExpectedRange)
{
    const auto vp = makeViewProj();
    const auto r = projectWorldToScreen(glm::vec3(0, 0, -5), vp, 1920, 1080);
    ASSERT_TRUE(r.visible);
    EXPECT_GE(r.ndcDepth, -1.0f);
    EXPECT_LE(r.ndcDepth,  1.0f);
}

TEST(UIWorldProjection, ZeroSizeViewportYieldsZeroPixelCoords)
{
    // Defensive: degenerate viewport shouldn't crash and should still report
    // visibility (the math is dimensionless until the final pixel-space scale).
    const auto vp = makeViewProj();
    const auto r = projectWorldToScreen(glm::vec3(0, 0, -5), vp, 0, 0);
    EXPECT_TRUE(r.visible);
    EXPECT_FLOAT_EQ(r.screenPos.x, 0.0f);
    EXPECT_FLOAT_EQ(r.screenPos.y, 0.0f);
}

// -- UIInteractionPrompt --

TEST(UIInteractionPrompt, ComposedTextFormat)
{
    UIInteractionPrompt p;
    p.keyLabel = "F";
    p.actionVerb = "open door";
    EXPECT_EQ(p.composedText(), "Press [F] to open door");
}

// The fade-alpha band has three regimes — full (≤ fadeNear), zero
// (≥ fadeFar), and a linear ramp between. The per-regime tests shared
// identical setup and differed only in {fadeNear, fadeFar, distance,
// expectedAlpha}, so they collapse into one table-driven TEST_P.
namespace
{
struct FadeCase
{
    const char* name;
    float fadeNear;
    float fadeFar;
    float distance;
    float expectedAlpha;
};

class UIInteractionPromptFade : public ::testing::TestWithParam<FadeCase> {};
} // namespace

TEST_P(UIInteractionPromptFade, ComputesAlphaAcrossFadeBand)
{
    const FadeCase& c = GetParam();
    UIInteractionPrompt p;
    p.fadeNear = c.fadeNear;
    p.fadeFar  = c.fadeFar;
    EXPECT_NEAR(p.computeFadeAlpha(c.distance), c.expectedAlpha, 1e-5f)
        << c.name << " (distance " << c.distance << ")";
}

INSTANTIATE_TEST_SUITE_P(
    FadeBand,
    UIInteractionPromptFade,
    ::testing::Values(
        FadeCase{"FullAtZero",            2.5f, 4.0f, 0.0f,  1.0f},
        FadeCase{"FullAtFadeNear",        2.5f, 4.0f, 2.5f,  1.0f},
        FadeCase{"FullJustBelowFadeNear", 2.5f, 4.0f, 2.49f, 1.0f},
        FadeCase{"ZeroAtFadeFar",         2.5f, 4.0f, 4.0f,  0.0f},
        FadeCase{"ZeroBeyondFadeFar",     2.5f, 4.0f, 10.0f, 0.0f},
        FadeCase{"HalfAtMidpoint",        2.0f, 4.0f, 3.0f,  0.5f}),
    [](const ::testing::TestParamInfo<FadeCase>& info)
    { return std::string(info.param.name); });

TEST(UIInteractionPrompt, NotInteractiveByDefault)
{
    UIInteractionPrompt p;
    EXPECT_FALSE(p.interactive);
}
