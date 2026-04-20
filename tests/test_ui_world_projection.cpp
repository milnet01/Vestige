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

TEST(UIInteractionPrompt, FadeAlphaFullBelowFadeNear)
{
    UIInteractionPrompt p;
    p.fadeNear = 2.5f;
    p.fadeFar  = 4.0f;
    EXPECT_FLOAT_EQ(p.computeFadeAlpha(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(p.computeFadeAlpha(2.5f), 1.0f);
    EXPECT_FLOAT_EQ(p.computeFadeAlpha(2.49f), 1.0f);
}

TEST(UIInteractionPrompt, FadeAlphaZeroAtAndBeyondFadeFar)
{
    UIInteractionPrompt p;
    p.fadeNear = 2.5f;
    p.fadeFar  = 4.0f;
    EXPECT_FLOAT_EQ(p.computeFadeAlpha(4.0f), 0.0f);
    EXPECT_FLOAT_EQ(p.computeFadeAlpha(10.0f), 0.0f);
}

TEST(UIInteractionPrompt, FadeAlphaLinearBetween)
{
    UIInteractionPrompt p;
    p.fadeNear = 2.0f;
    p.fadeFar  = 4.0f;
    // Midpoint = distance 3.0 → expected alpha 0.5.
    EXPECT_NEAR(p.computeFadeAlpha(3.0f), 0.5f, 1e-5f);
}

TEST(UIInteractionPrompt, NotInteractiveByDefault)
{
    UIInteractionPrompt p;
    EXPECT_FALSE(p.interactive);
}
