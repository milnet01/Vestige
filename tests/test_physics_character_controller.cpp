// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_physics_character_controller.cpp
/// @brief Unit tests for the PhysicsCharacterController.
#include "physics/physics_character_controller.h"
#include "physics/physics_world.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Test fixture — creates a PhysicsWorld with a static floor at Y=0
// ---------------------------------------------------------------------------

class PhysicsCharControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(world.initialize());

        // Large static floor: top surface at Y=0
        JPH::BoxShape* floor = new JPH::BoxShape(JPH::Vec3(100, 0.5f, 100));
        world.createStaticBody(floor, glm::vec3(0, -0.5f, 0));
    }

    void TearDown() override
    {
        world.shutdown();
    }

    PhysicsWorld world;
};

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST_F(PhysicsCharControllerTest, InitializeAndShutdown)
{
    PhysicsCharacterController ctrl;
    EXPECT_FALSE(ctrl.isInitialized());

    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 0, 0)));
    EXPECT_TRUE(ctrl.isInitialized());

    ctrl.shutdown();
    EXPECT_FALSE(ctrl.isInitialized());
}

TEST_F(PhysicsCharControllerTest, DoubleInitializeIsSafe)
{
    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 0, 0)));
    EXPECT_TRUE(ctrl.initialize(world, glm::vec3(5, 0, 5)));
    EXPECT_TRUE(ctrl.isInitialized());
}

TEST_F(PhysicsCharControllerTest, InitWithUninitializedWorldFails)
{
    PhysicsWorld emptyWorld;
    PhysicsCharacterController ctrl;
    EXPECT_FALSE(ctrl.initialize(emptyWorld, glm::vec3(0, 0, 0)));
    EXPECT_FALSE(ctrl.isInitialized());
}

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------

TEST_F(PhysicsCharControllerTest, PositionSetGet)
{
    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(1, 2, 3)));

    glm::vec3 pos = ctrl.getPosition();
    EXPECT_NEAR(pos.x, 1.0f, 0.1f);
    EXPECT_NEAR(pos.y, 2.0f, 0.1f);
    EXPECT_NEAR(pos.z, 3.0f, 0.1f);

    ctrl.setPosition(glm::vec3(10, 5, 10));
    pos = ctrl.getPosition();
    EXPECT_NEAR(pos.x, 10.0f, 0.1f);
    EXPECT_NEAR(pos.y, 5.0f, 0.1f);
    EXPECT_NEAR(pos.z, 10.0f, 0.1f);
}

TEST_F(PhysicsCharControllerTest, EyePositionOffset)
{
    PhysicsControllerConfig config;
    config.eyeHeight = 1.7f;

    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 0, 0), config));

    glm::vec3 eye = ctrl.getEyePosition();
    glm::vec3 feet = ctrl.getPosition();

    EXPECT_NEAR(eye.y - feet.y, 1.7f, 0.01f);
    EXPECT_NEAR(eye.x, feet.x, 0.01f);
    EXPECT_NEAR(eye.z, feet.z, 0.01f);
}

// ---------------------------------------------------------------------------
// Fly mode
// ---------------------------------------------------------------------------

TEST_F(PhysicsCharControllerTest, FlyModeToggle)
{
    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 5, 0)));

    EXPECT_FALSE(ctrl.isFlyMode());
    ctrl.setFlyMode(true);
    EXPECT_TRUE(ctrl.isFlyMode());
    ctrl.setFlyMode(false);
    EXPECT_FALSE(ctrl.isFlyMode());
}

TEST_F(PhysicsCharControllerTest, FlyModeIgnoresGravity)
{
    PhysicsControllerConfig config;
    config.gravity = glm::vec3(0, -9.81f, 0);

    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 5, 0), config));
    ctrl.setFlyMode(true);

    float startY = ctrl.getPosition().y;

    // Step with zero velocity — should hover, not fall
    for (int i = 0; i < 60; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f));
    }

    EXPECT_NEAR(ctrl.getPosition().y, startY, 0.1f);
}

TEST_F(PhysicsCharControllerTest, FlyModeMovesVertically)
{
    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 5, 0)));
    ctrl.setFlyMode(true);

    float startY = ctrl.getPosition().y;

    // Move upward
    for (int i = 0; i < 30; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f, 3.0f, 0.0f));
    }

    EXPECT_GT(ctrl.getPosition().y, startY + 0.5f);
}

// ---------------------------------------------------------------------------
// Gravity and ground detection
// ---------------------------------------------------------------------------

TEST_F(PhysicsCharControllerTest, GravityPullsDown)
{
    PhysicsControllerConfig config;
    config.gravity = glm::vec3(0, -9.81f, 0);

    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 5, 0), config));

    float startY = ctrl.getPosition().y;

    for (int i = 0; i < 60; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f));
    }

    EXPECT_LT(ctrl.getPosition().y, startY);
}

TEST_F(PhysicsCharControllerTest, GroundStateOnFloor)
{
    PhysicsControllerConfig config;
    config.gravity = glm::vec3(0, -9.81f, 0);

    PhysicsCharacterController ctrl;
    // Start at floor level
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 0, 0), config));

    // Settle onto the floor
    for (int i = 0; i < 30; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f));
    }

    EXPECT_TRUE(ctrl.isOnGround());
    EXPECT_FALSE(ctrl.isInAir());
}

TEST_F(PhysicsCharControllerTest, SettlesOnFloor)
{
    PhysicsControllerConfig config;
    config.gravity = glm::vec3(0, -9.81f, 0);

    PhysicsCharacterController ctrl;
    // Start above the floor
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 3, 0), config));

    // Fall and settle
    for (int i = 0; i < 180; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f));
    }

    // Should be on or near floor level (Y=0)
    EXPECT_NEAR(ctrl.getPosition().y, 0.0f, 0.5f);
    EXPECT_TRUE(ctrl.isOnGround());
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

TEST_F(PhysicsCharControllerTest, WalkForwardOnFloor)
{
    PhysicsControllerConfig config;
    config.gravity = glm::vec3(0, -9.81f, 0);

    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 0, 0), config));

    // Settle first
    for (int i = 0; i < 10; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f));
    }

    // Walk in +Z
    for (int i = 0; i < 60; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f, 0.0f, 3.0f));
    }

    // Should have moved forward
    EXPECT_GT(ctrl.getPosition().z, 0.5f);
    // Should still be near floor level
    EXPECT_NEAR(ctrl.getPosition().y, 0.0f, 0.5f);
}

TEST_F(PhysicsCharControllerTest, WallCollisionStopsMovement)
{
    // Create a wall at X=3
    JPH::BoxShape* wall = new JPH::BoxShape(JPH::Vec3(0.5f, 5.0f, 5.0f));
    world.createStaticBody(wall, glm::vec3(3, 5, 0));

    PhysicsControllerConfig config;
    config.gravity = glm::vec3(0, -9.81f, 0);

    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 0, 0), config));

    // Settle
    for (int i = 0; i < 10; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f));
    }

    // Walk toward the wall in +X
    for (int i = 0; i < 120; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(3.0f, 0.0f, 0.0f));
    }

    // Should be blocked before the wall face (wall at x=3, half-extent 0.5 -> face at x=2.5)
    // Character capsule radius = 0.3, so max x ~ 2.5 - 0.3 - padding = ~2.2
    EXPECT_LT(ctrl.getPosition().x, 2.6f);
    EXPECT_GT(ctrl.getPosition().x, 1.0f);  // Should have moved toward it
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

TEST_F(PhysicsCharControllerTest, LinearVelocityReported)
{
    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 5, 0)));
    ctrl.setFlyMode(true);

    // Apply movement
    ctrl.update(1.0f / 60.0f, glm::vec3(3.0f, 0.0f, 0.0f));

    glm::vec3 vel = ctrl.getLinearVelocity();
    EXPECT_GT(std::abs(vel.x), 0.1f);
}

// ---------------------------------------------------------------------------
// Uninitialized safety
// ---------------------------------------------------------------------------

TEST_F(PhysicsCharControllerTest, UninitializedSafety)
{
    PhysicsCharacterController ctrl;

    // All methods should be safe to call without initialization
    EXPECT_EQ(ctrl.getPosition(), glm::vec3(0.0f));
    EXPECT_EQ(ctrl.getEyePosition(), glm::vec3(0.0f, 1.7f, 0.0f));
    EXPECT_EQ(ctrl.getLinearVelocity(), glm::vec3(0.0f));
    EXPECT_FALSE(ctrl.isOnGround());
    EXPECT_TRUE(ctrl.isInAir());

    // These should not crash
    ctrl.update(1.0f / 60.0f, glm::vec3(1, 0, 0));
    ctrl.setPosition(glm::vec3(5, 5, 5));
    ctrl.setFlyMode(true);
    ctrl.shutdown();
}
