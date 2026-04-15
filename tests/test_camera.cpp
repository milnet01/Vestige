// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_camera.cpp
/// @brief Unit tests for the Camera class (pure-math, no OpenGL context needed).
#include "renderer/camera.h"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace Vestige;

// Helper: floating-point tolerance for matrix/vector comparisons.
constexpr float EPSILON = 1e-4f;

// ============================================================
// Default state
// ============================================================

TEST(CameraTest, DefaultPosition)
{
    Camera cam;
    glm::vec3 pos = cam.getPosition();
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);
}

TEST(CameraTest, DefaultFov)
{
    Camera cam;
    EXPECT_FLOAT_EQ(cam.getFov(), DEFAULT_FOV);
}

TEST(CameraTest, DefaultYawAndPitch)
{
    Camera cam;
    EXPECT_FLOAT_EQ(cam.getYaw(), DEFAULT_YAW);
    EXPECT_FLOAT_EQ(cam.getPitch(), DEFAULT_PITCH);
}

TEST(CameraTest, DefaultFrontVector)
{
    // yaw=-90, pitch=0 => front = (0, 0, -1)
    Camera cam;
    glm::vec3 front = cam.getFront();
    EXPECT_NEAR(front.x, 0.0f, EPSILON);
    EXPECT_NEAR(front.y, 0.0f, EPSILON);
    EXPECT_NEAR(front.z, -1.0f, EPSILON);
}

// ============================================================
// Custom construction
// ============================================================

TEST(CameraTest, CustomPositionAndAngles)
{
    Camera cam(glm::vec3(1.0f, 2.0f, 3.0f), 0.0f, 45.0f);
    glm::vec3 pos = cam.getPosition();
    EXPECT_FLOAT_EQ(pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.0f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);
    EXPECT_FLOAT_EQ(cam.getYaw(), 0.0f);
    EXPECT_FLOAT_EQ(cam.getPitch(), 45.0f);
}

TEST(CameraTest, FrontVectorWithYawZeroPitchZero)
{
    // yaw=0, pitch=0 => front = (1, 0, 0)  (pointing along +X)
    Camera cam(glm::vec3(0.0f), 0.0f, 0.0f);
    glm::vec3 front = cam.getFront();
    EXPECT_NEAR(front.x, 1.0f, EPSILON);
    EXPECT_NEAR(front.y, 0.0f, EPSILON);
    EXPECT_NEAR(front.z, 0.0f, EPSILON);
}

// ============================================================
// Getters / setters
// ============================================================

TEST(CameraTest, SetPositionChangesPosition)
{
    Camera cam;
    cam.setPosition(glm::vec3(10.0f, 20.0f, 30.0f));
    glm::vec3 pos = cam.getPosition();
    EXPECT_FLOAT_EQ(pos.x, 10.0f);
    EXPECT_FLOAT_EQ(pos.y, 20.0f);
    EXPECT_FLOAT_EQ(pos.z, 30.0f);
}

TEST(CameraTest, SetYawUpdatesFront)
{
    Camera cam;
    cam.setYaw(0.0f);
    EXPECT_FLOAT_EQ(cam.getYaw(), 0.0f);
    // yaw=0 => front.x = cos(0)*cos(pitch) = 1, front.z = sin(0)*cos(pitch) = 0
    glm::vec3 front = cam.getFront();
    EXPECT_NEAR(front.x, 1.0f, EPSILON);
    EXPECT_NEAR(front.z, 0.0f, EPSILON);
}

TEST(CameraTest, SetPitchUpdatesFront)
{
    Camera cam;  // yaw=-90
    cam.setPitch(45.0f);
    EXPECT_FLOAT_EQ(cam.getPitch(), 45.0f);
    glm::vec3 front = cam.getFront();
    float expected_y = std::sin(glm::radians(45.0f));
    EXPECT_NEAR(front.y, expected_y, EPSILON);
}

TEST(CameraTest, SetPitchClampedAbove)
{
    Camera cam;
    cam.setPitch(100.0f);
    EXPECT_FLOAT_EQ(cam.getPitch(), 89.0f);
}

TEST(CameraTest, SetPitchClampedBelow)
{
    Camera cam;
    cam.setPitch(-100.0f);
    EXPECT_FLOAT_EQ(cam.getPitch(), -89.0f);
}

// ============================================================
// View matrix
// ============================================================

TEST(CameraTest, ViewMatrixMatchesGlmLookAt)
{
    Camera cam(glm::vec3(0.0f, 0.0f, 3.0f));
    glm::mat4 view = cam.getViewMatrix();

    // Manually compute expected lookAt with default front (0,0,-1)
    glm::vec3 pos(0.0f, 0.0f, 3.0f);
    glm::vec3 front = cam.getFront();
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    // Camera up is cross(right, front), but lookAt takes worldUp-derived up.
    // We verify against glm::lookAt using the camera's actual up.
    glm::mat4 expected = glm::lookAt(pos, pos + front, up);

    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(view[col][row], expected[col][row], EPSILON)
                << "Mismatch at [" << col << "][" << row << "]";
        }
    }
}

TEST(CameraTest, ViewMatrixChangesWithPosition)
{
    Camera cam;
    glm::mat4 view1 = cam.getViewMatrix();

    cam.setPosition(glm::vec3(5.0f, 5.0f, 5.0f));
    glm::mat4 view2 = cam.getViewMatrix();

    // Translation column (column 3) should differ
    bool differs = false;
    for (int row = 0; row < 4; ++row)
    {
        if (std::abs(view1[3][row] - view2[3][row]) > EPSILON)
        {
            differs = true;
            break;
        }
    }
    EXPECT_TRUE(differs);
}

TEST(CameraTest, ViewMatrixChangesWithYaw)
{
    Camera cam;
    glm::mat4 view1 = cam.getViewMatrix();

    cam.setYaw(45.0f);
    glm::mat4 view2 = cam.getViewMatrix();

    // Rotation part should differ
    bool differs = false;
    for (int col = 0; col < 3; ++col)
    {
        for (int row = 0; row < 3; ++row)
        {
            if (std::abs(view1[col][row] - view2[col][row]) > EPSILON)
            {
                differs = true;
                break;
            }
        }
    }
    EXPECT_TRUE(differs);
}

// ============================================================
// Projection matrix (reverse-Z infinite far)
// ============================================================

TEST(CameraTest, ProjectionMatrixStructure)
{
    Camera cam;
    float aspect = 16.0f / 9.0f;
    glm::mat4 proj = cam.getProjectionMatrix(aspect);

    float fovRad = glm::radians(DEFAULT_FOV);
    float f = 1.0f / std::tan(fovRad * 0.5f);
    float nearPlane = 0.1f;

    // Check key elements of the reverse-Z infinite projection
    EXPECT_NEAR(proj[0][0], f / aspect, EPSILON);
    EXPECT_NEAR(proj[1][1], f, EPSILON);
    EXPECT_NEAR(proj[2][3], -1.0f, EPSILON);    // perspective divide: w = -z
    EXPECT_NEAR(proj[3][2], nearPlane, EPSILON); // near maps to depth 1.0

    // Zero elements that should be zero
    EXPECT_FLOAT_EQ(proj[0][1], 0.0f);
    EXPECT_FLOAT_EQ(proj[0][2], 0.0f);
    EXPECT_FLOAT_EQ(proj[0][3], 0.0f);
    EXPECT_FLOAT_EQ(proj[1][0], 0.0f);
    EXPECT_FLOAT_EQ(proj[1][2], 0.0f);
    EXPECT_FLOAT_EQ(proj[1][3], 0.0f);
    EXPECT_FLOAT_EQ(proj[2][0], 0.0f);
    EXPECT_FLOAT_EQ(proj[2][1], 0.0f);
    EXPECT_FLOAT_EQ(proj[2][2], 0.0f);
    EXPECT_FLOAT_EQ(proj[3][0], 0.0f);
    EXPECT_FLOAT_EQ(proj[3][1], 0.0f);
    EXPECT_FLOAT_EQ(proj[3][3], 0.0f);
}

TEST(CameraTest, ProjectionMatrixChangesWithAspect)
{
    Camera cam;
    glm::mat4 proj16x9 = cam.getProjectionMatrix(16.0f / 9.0f);
    glm::mat4 proj4x3 = cam.getProjectionMatrix(4.0f / 3.0f);

    // [0][0] = f / aspect, so wider aspect => smaller [0][0]
    EXPECT_LT(proj16x9[0][0], proj4x3[0][0]);

    // [1][1] should be identical (independent of aspect)
    EXPECT_FLOAT_EQ(proj16x9[1][1], proj4x3[1][1]);
}

// ============================================================
// Culling projection matrix (standard GLM perspective)
// ============================================================

TEST(CameraTest, CullingProjectionMatchesGlmPerspective)
{
    Camera cam;
    float aspect = 16.0f / 9.0f;
    glm::mat4 cull = cam.getCullingProjectionMatrix(aspect);
    glm::mat4 expected = glm::perspective(glm::radians(DEFAULT_FOV), aspect, 0.1f, 1000.0f);

    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(cull[col][row], expected[col][row], EPSILON)
                << "Mismatch at [" << col << "][" << row << "]";
        }
    }
}

// ============================================================
// Movement
// ============================================================

TEST(CameraTest, MoveForwardChangesPosition)
{
    Camera cam(glm::vec3(0.0f));
    cam.setSpeed(1.0f);
    glm::vec3 frontBefore = cam.getFront();

    cam.move(1.0f, 0.0f, 0.0f, 1.0f);  // forward=1, dt=1

    glm::vec3 pos = cam.getPosition();
    // Position should move along the front vector by speed * forward * dt
    EXPECT_NEAR(pos.x, frontBefore.x, EPSILON);
    EXPECT_NEAR(pos.y, frontBefore.y, EPSILON);
    EXPECT_NEAR(pos.z, frontBefore.z, EPSILON);
}

TEST(CameraTest, MoveBackwardChangesPosition)
{
    Camera cam(glm::vec3(0.0f));
    cam.setSpeed(1.0f);
    glm::vec3 frontBefore = cam.getFront();

    cam.move(-1.0f, 0.0f, 0.0f, 1.0f);  // backward

    glm::vec3 pos = cam.getPosition();
    EXPECT_NEAR(pos.x, -frontBefore.x, EPSILON);
    EXPECT_NEAR(pos.y, -frontBefore.y, EPSILON);
    EXPECT_NEAR(pos.z, -frontBefore.z, EPSILON);
}

TEST(CameraTest, MoveRightChangesPosition)
{
    Camera cam(glm::vec3(0.0f));
    cam.setSpeed(1.0f);

    cam.move(0.0f, 1.0f, 0.0f, 1.0f);  // right=1

    glm::vec3 pos = cam.getPosition();
    // Default cam faces -Z, so right should be +X
    EXPECT_GT(pos.x, 0.0f);
    EXPECT_NEAR(pos.y, 0.0f, EPSILON);
}

TEST(CameraTest, MoveUpChangesPosition)
{
    Camera cam(glm::vec3(0.0f));
    cam.setSpeed(1.0f);

    cam.move(0.0f, 0.0f, 1.0f, 1.0f);  // up=1

    glm::vec3 pos = cam.getPosition();
    EXPECT_NEAR(pos.x, 0.0f, EPSILON);
    EXPECT_GT(pos.y, 0.0f);
    EXPECT_NEAR(pos.z, 0.0f, EPSILON);
}

TEST(CameraTest, MoveScalesWithDeltaTime)
{
    Camera cam1(glm::vec3(0.0f));
    Camera cam2(glm::vec3(0.0f));
    cam1.setSpeed(1.0f);
    cam2.setSpeed(1.0f);

    cam1.move(1.0f, 0.0f, 0.0f, 0.5f);
    cam2.move(1.0f, 0.0f, 0.0f, 1.0f);

    glm::vec3 p1 = cam1.getPosition();
    glm::vec3 p2 = cam2.getPosition();
    // Half deltaTime => half distance
    EXPECT_NEAR(glm::length(p1), glm::length(p2) * 0.5f, EPSILON);
}

TEST(CameraTest, MoveScalesWithSpeed)
{
    Camera cam1(glm::vec3(0.0f));
    Camera cam2(glm::vec3(0.0f));
    cam1.setSpeed(2.0f);
    cam2.setSpeed(4.0f);

    cam1.move(1.0f, 0.0f, 0.0f, 1.0f);
    cam2.move(1.0f, 0.0f, 0.0f, 1.0f);

    float dist1 = glm::length(cam1.getPosition());
    float dist2 = glm::length(cam2.getPosition());
    EXPECT_NEAR(dist2, dist1 * 2.0f, EPSILON);
}

TEST(CameraTest, ZeroDeltaTimeNoMovement)
{
    Camera cam(glm::vec3(5.0f, 5.0f, 5.0f));
    cam.move(1.0f, 1.0f, 1.0f, 0.0f);

    glm::vec3 pos = cam.getPosition();
    EXPECT_FLOAT_EQ(pos.x, 5.0f);
    EXPECT_FLOAT_EQ(pos.y, 5.0f);
    EXPECT_FLOAT_EQ(pos.z, 5.0f);
}

// ============================================================
// Rotation
// ============================================================

TEST(CameraTest, RotateChangesYawAndPitch)
{
    Camera cam;
    cam.setSensitivity(1.0f);
    float yawBefore = cam.getYaw();
    float pitchBefore = cam.getPitch();

    cam.rotate(10.0f, 5.0f);

    EXPECT_NEAR(cam.getYaw(), yawBefore + 10.0f, EPSILON);
    EXPECT_NEAR(cam.getPitch(), pitchBefore + 5.0f, EPSILON);
}

TEST(CameraTest, RotateSensitivityScaling)
{
    Camera cam;
    cam.setSensitivity(0.5f);
    float yawBefore = cam.getYaw();

    cam.rotate(20.0f, 0.0f);

    EXPECT_NEAR(cam.getYaw(), yawBefore + 10.0f, EPSILON);  // 20 * 0.5
}

TEST(CameraTest, RotatePitchClampedAt89)
{
    Camera cam;
    cam.setSensitivity(1.0f);

    // Pitch starts at 0, rotate up by 100 degrees — should clamp to 89
    cam.rotate(0.0f, 100.0f);
    EXPECT_FLOAT_EQ(cam.getPitch(), 89.0f);

    // Now rotate down by 200 — should clamp to -89
    cam.rotate(0.0f, -200.0f);
    EXPECT_FLOAT_EQ(cam.getPitch(), -89.0f);
}

TEST(CameraTest, RotateUpdatesFrontVector)
{
    Camera cam;
    cam.setSensitivity(1.0f);
    glm::vec3 frontBefore = cam.getFront();

    cam.rotate(90.0f, 0.0f);  // Rotate 90 degrees in yaw

    glm::vec3 frontAfter = cam.getFront();
    // Front vector should have changed significantly
    float dot = glm::dot(frontBefore, frontAfter);
    EXPECT_LT(std::abs(dot), 0.1f);  // Nearly perpendicular
}

// ============================================================
// FOV adjustment
// ============================================================

TEST(CameraTest, AdjustFovDecreasesOnPositiveOffset)
{
    Camera cam;
    float fovBefore = cam.getFov();
    cam.adjustFov(5.0f);
    EXPECT_FLOAT_EQ(cam.getFov(), fovBefore - 5.0f);
}

TEST(CameraTest, AdjustFovIncreasesOnNegativeOffset)
{
    Camera cam;
    float fovBefore = cam.getFov();
    cam.adjustFov(-5.0f);
    EXPECT_FLOAT_EQ(cam.getFov(), fovBefore + 5.0f);
}

TEST(CameraTest, AdjustFovClampedMin)
{
    Camera cam;
    cam.adjustFov(1000.0f);  // Way past minimum
    EXPECT_FLOAT_EQ(cam.getFov(), 1.0f);
}

TEST(CameraTest, AdjustFovClampedMax)
{
    Camera cam;
    cam.adjustFov(-1000.0f);  // Way past maximum
    EXPECT_FLOAT_EQ(cam.getFov(), 120.0f);
}

TEST(CameraTest, AdjustFovAffectsProjection)
{
    Camera cam;
    float aspect = 1.0f;
    glm::mat4 proj1 = cam.getProjectionMatrix(aspect);

    cam.adjustFov(10.0f);
    glm::mat4 proj2 = cam.getProjectionMatrix(aspect);

    // Wider FOV (smaller fov value) => larger [1][1] (f = 1/tan(fov/2))
    // We decreased FOV by 10, so f increases, so [1][1] should be larger
    EXPECT_GT(proj2[1][1], proj1[1][1]);
}

// ============================================================
// Edge cases
// ============================================================

TEST(CameraTest, FrontVectorAlwaysNormalized)
{
    Camera cam;
    // Test across various yaw/pitch combinations
    float yaws[] = {-180.0f, -90.0f, 0.0f, 45.0f, 90.0f, 180.0f, 270.0f};
    float pitches[] = {-89.0f, -45.0f, 0.0f, 45.0f, 89.0f};

    for (float yaw : yaws)
    {
        for (float pitch : pitches)
        {
            cam.setYaw(yaw);
            cam.setPitch(pitch);
            float len = glm::length(cam.getFront());
            EXPECT_NEAR(len, 1.0f, EPSILON)
                << "Front not normalized at yaw=" << yaw << " pitch=" << pitch;
        }
    }
}

TEST(CameraTest, ExtremeFovProjectionStillValid)
{
    Camera cam;

    // Minimum FOV (1 degree) — very zoomed in
    cam.adjustFov(1000.0f);  // clamps to 1.0
    glm::mat4 proj1 = cam.getProjectionMatrix(1.0f);
    EXPECT_FALSE(std::isnan(proj1[0][0]));
    EXPECT_FALSE(std::isinf(proj1[0][0]));
    EXPECT_GT(proj1[1][1], 0.0f);

    // Maximum FOV (120 degrees) — very wide
    cam.adjustFov(-1000.0f);  // clamps to 120
    glm::mat4 proj120 = cam.getProjectionMatrix(1.0f);
    EXPECT_FALSE(std::isnan(proj120[0][0]));
    EXPECT_FALSE(std::isinf(proj120[0][0]));
    EXPECT_GT(proj120[1][1], 0.0f);
}

TEST(CameraTest, PitchAtExtremesCameraStillUsable)
{
    // At pitch=89, camera looks nearly straight up — vectors should still be valid
    Camera cam(glm::vec3(0.0f), -90.0f, 89.0f);
    glm::vec3 front = cam.getFront();
    EXPECT_NEAR(glm::length(front), 1.0f, EPSILON);
    EXPECT_GT(front.y, 0.9f);  // Mostly pointing up

    // View matrix should not contain NaN
    glm::mat4 view = cam.getViewMatrix();
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_FALSE(std::isnan(view[col][row]))
                << "NaN in view matrix at [" << col << "][" << row << "]";
        }
    }
}

TEST(CameraTest, NegativePitchExtremeCameraStillUsable)
{
    Camera cam(glm::vec3(0.0f), -90.0f, -89.0f);
    glm::vec3 front = cam.getFront();
    EXPECT_NEAR(glm::length(front), 1.0f, EPSILON);
    EXPECT_LT(front.y, -0.9f);  // Mostly pointing down

    glm::mat4 view = cam.getViewMatrix();
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_FALSE(std::isnan(view[col][row]));
        }
    }
}

TEST(CameraTest, VerySmallAspectRatioProjection)
{
    Camera cam;
    glm::mat4 proj = cam.getProjectionMatrix(0.01f);  // Very tall, narrow viewport
    EXPECT_FALSE(std::isnan(proj[0][0]));
    EXPECT_FALSE(std::isinf(proj[0][0]));
    // [0][0] = f / aspect should be very large
    EXPECT_GT(proj[0][0], 100.0f);
}

TEST(CameraTest, LargeAspectRatioProjection)
{
    Camera cam;
    glm::mat4 proj = cam.getProjectionMatrix(100.0f);  // Ultra-wide
    EXPECT_FALSE(std::isnan(proj[0][0]));
    EXPECT_FALSE(std::isinf(proj[0][0]));
    // [0][0] = f / aspect should be very small
    EXPECT_LT(proj[0][0], 0.1f);
}
