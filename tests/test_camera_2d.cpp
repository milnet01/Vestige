// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_camera_2d.cpp
/// @brief Unit tests for Camera2DComponent + updateCamera2DFollow (Phase 9F-4).
#include "scene/camera_2d_component.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(Camera2D, FirstFrameSnapsToTarget)
{
    Camera2DComponent cam;
    cam.smoothTimeSec = 0.2f;
    updateCamera2DFollow(cam, glm::vec2(5.0f, 3.0f), 0.016f);
    EXPECT_FLOAT_EQ(cam.position.x, 5.0f);
    EXPECT_FLOAT_EQ(cam.position.y, 3.0f);
    EXPECT_TRUE(cam.hasInitialized);
}

TEST(Camera2D, DeadzoneSuppressesSmallMovement)
{
    Camera2DComponent cam;
    cam.deadzoneHalfSize = glm::vec2(2.0f, 1.0f);
    cam.smoothTimeSec = 0.2f;
    updateCamera2DFollow(cam, glm::vec2(0.0f, 0.0f), 0.016f);

    // Target wanders within the deadzone — camera should not move.
    for (int i = 0; i < 30; ++i)
    {
        updateCamera2DFollow(cam, glm::vec2(1.0f, 0.5f), 0.016f);
    }
    EXPECT_NEAR(cam.position.x, 0.0f, 0.01f);
    EXPECT_NEAR(cam.position.y, 0.0f, 0.01f);
}

TEST(Camera2D, FollowsBeyondDeadzone)
{
    Camera2DComponent cam;
    cam.deadzoneHalfSize = glm::vec2(1.0f, 0.5f);
    cam.smoothTimeSec = 0.1f;  // snappy
    updateCamera2DFollow(cam, glm::vec2(0.0f, 0.0f), 0.016f);

    for (int i = 0; i < 120; ++i)
    {
        updateCamera2DFollow(cam, glm::vec2(10.0f, 0.0f), 0.016f);
    }
    // Camera lags by exactly the deadzone half-size (target X minus dz).
    EXPECT_NEAR(cam.position.x, 9.0f, 0.2f);
}

TEST(Camera2D, ClampsToWorldBounds)
{
    Camera2DComponent cam;
    cam.clampToBounds = true;
    cam.worldBounds = glm::vec4(-5.0f, -5.0f, 5.0f, 5.0f);
    cam.smoothTimeSec = 0.0f;  // instant follow

    // Initial update snaps to inside bounds.
    updateCamera2DFollow(cam, glm::vec2(0.0f, 0.0f), 0.016f);
    // Subsequent far-off target is clamped.
    updateCamera2DFollow(cam, glm::vec2(100.0f, -100.0f), 0.016f);
    EXPECT_LE(cam.position.x, 5.0f);
    EXPECT_GE(cam.position.y, -5.0f);
}

TEST(Camera2D, ZeroSmoothTimeSnapsInstantly)
{
    Camera2DComponent cam;
    cam.smoothTimeSec = 0.0f;
    updateCamera2DFollow(cam, glm::vec2(1.0f, 2.0f), 0.016f);
    updateCamera2DFollow(cam, glm::vec2(10.0f, -5.0f), 0.016f);
    EXPECT_FLOAT_EQ(cam.position.x, 10.0f);
    EXPECT_FLOAT_EQ(cam.position.y, -5.0f);
}

TEST(Camera2D, FollowOffsetAppliedBeforeDeadzone)
{
    Camera2DComponent cam;
    cam.followOffset = glm::vec2(0.0f, 2.0f);
    cam.smoothTimeSec = 0.0f;
    updateCamera2DFollow(cam, glm::vec2(0.0f, 0.0f), 0.016f);
    // Camera is snapped to target + offset = (0, 2).
    EXPECT_FLOAT_EQ(cam.position.y, 2.0f);
}

TEST(Camera2D, CloneResetsTransientState)
{
    Camera2DComponent cam;
    cam.smoothTimeSec = 0.2f;
    updateCamera2DFollow(cam, glm::vec2(10.0f, 0.0f), 0.016f);
    cam.velocity = glm::vec2(3.0f, 0.0f);
    ASSERT_TRUE(cam.hasInitialized);

    auto cloned = cam.clone();
    auto* c = static_cast<Camera2DComponent*>(cloned.get());
    EXPECT_FLOAT_EQ(c->velocity.x, 0.0f);
    EXPECT_FALSE(c->hasInitialized);
}
