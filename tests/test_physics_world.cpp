// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_physics_world.cpp
/// @brief Unit tests for the PhysicsWorld subsystem.
#include "physics/physics_world.h"
#include "physics/jolt_helpers.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <gtest/gtest.h>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Initialization and shutdown
// ---------------------------------------------------------------------------

TEST(PhysicsWorld, InitializeAndShutdown)
{
    PhysicsWorld world;
    EXPECT_FALSE(world.isInitialized());

    EXPECT_TRUE(world.initialize());
    EXPECT_TRUE(world.isInitialized());

    world.shutdown();
    EXPECT_FALSE(world.isInitialized());
}

TEST(PhysicsWorld, DoubleInitializeIsSafe)
{
    PhysicsWorld world;
    EXPECT_TRUE(world.initialize());
    EXPECT_TRUE(world.initialize());  // Should warn but not crash
    world.shutdown();
}

TEST(PhysicsWorld, ShutdownWithoutInitializeIsSafe)
{
    PhysicsWorld world;
    world.shutdown();  // Should be a no-op
}

// ---------------------------------------------------------------------------
// Body creation
// ---------------------------------------------------------------------------

TEST(PhysicsWorld, CreateStaticBody)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    JPH::BoxShape* box = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
    JPH::BodyID id = world.createStaticBody(box, glm::vec3(0, 0, 0));
    EXPECT_FALSE(id.IsInvalid());

    glm::vec3 pos = world.getBodyPosition(id);
    EXPECT_NEAR(pos.x, 0.0f, 0.001f);
    EXPECT_NEAR(pos.y, 0.0f, 0.001f);
    EXPECT_NEAR(pos.z, 0.0f, 0.001f);

    world.destroyBody(id);
    world.shutdown();
}

TEST(PhysicsWorld, CreateDynamicBody)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    JPH::SphereShape* sphere = new JPH::SphereShape(0.5f);
    JPH::BodyID id = world.createDynamicBody(sphere, glm::vec3(0, 10, 0));
    EXPECT_FALSE(id.IsInvalid());

    // Active body count should be 1
    EXPECT_EQ(world.getActiveBodyCount(), 1u);

    world.destroyBody(id);
    world.shutdown();
}

TEST(PhysicsWorld, CreateKinematicBody)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    JPH::BoxShape* box = new JPH::BoxShape(JPH::Vec3(1, 1, 1));
    JPH::BodyID id = world.createKinematicBody(box, glm::vec3(5, 0, 0));
    EXPECT_FALSE(id.IsInvalid());

    // Kinematic bodies are active
    EXPECT_GE(world.getActiveBodyCount(), 1u);

    world.destroyBody(id);
    world.shutdown();
}

// ---------------------------------------------------------------------------
// Physics step
// ---------------------------------------------------------------------------

TEST(PhysicsWorld, DynamicBodyFallsUnderGravity)
{
    PhysicsWorld world;
    PhysicsWorldConfig config;
    config.fixedTimestep = 1.0f / 60.0f;
    ASSERT_TRUE(world.initialize(config));

    // Create a floor
    JPH::BoxShape* floor = new JPH::BoxShape(JPH::Vec3(50, 0.5f, 50));
    world.createStaticBody(floor, glm::vec3(0, -0.5f, 0));

    // Create a dynamic sphere above the floor
    JPH::SphereShape* sphere = new JPH::SphereShape(0.25f);
    JPH::BodyID ballId = world.createDynamicBody(sphere, glm::vec3(0, 5, 0));

    float initialY = world.getBodyPosition(ballId).y;
    EXPECT_NEAR(initialY, 5.0f, 0.01f);

    // Step for 1 second (60 steps at 1/60)
    for (int i = 0; i < 60; ++i)
    {
        world.update(1.0f / 60.0f);
    }

    float finalY = world.getBodyPosition(ballId).y;
    // Ball should have fallen significantly (but stopped on the floor)
    EXPECT_LT(finalY, initialY);

    world.shutdown();
}

TEST(PhysicsWorld, StaticBodyDoesNotMove)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    JPH::BoxShape* box = new JPH::BoxShape(JPH::Vec3(1, 1, 1));
    JPH::BodyID id = world.createStaticBody(box, glm::vec3(3, 7, -2));

    // Step physics
    for (int i = 0; i < 60; ++i)
    {
        world.update(1.0f / 60.0f);
    }

    glm::vec3 pos = world.getBodyPosition(id);
    EXPECT_NEAR(pos.x, 3.0f, 0.001f);
    EXPECT_NEAR(pos.y, 7.0f, 0.001f);
    EXPECT_NEAR(pos.z, -2.0f, 0.001f);

    world.shutdown();
}

// ---------------------------------------------------------------------------
// Body manipulation
// ---------------------------------------------------------------------------

TEST(PhysicsWorld, SetKinematicBodyTransform)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    JPH::BoxShape* box = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
    JPH::BodyID id = world.createKinematicBody(box, glm::vec3(0, 0, 0));

    world.setBodyTransform(id, glm::vec3(10, 20, 30), glm::quat(1, 0, 0, 0));

    glm::vec3 pos = world.getBodyPosition(id);
    EXPECT_NEAR(pos.x, 10.0f, 0.01f);
    EXPECT_NEAR(pos.y, 20.0f, 0.01f);
    EXPECT_NEAR(pos.z, 30.0f, 0.01f);

    world.shutdown();
}

TEST(PhysicsWorld, ApplyImpulse)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    JPH::SphereShape* sphere = new JPH::SphereShape(0.5f);
    JPH::BodyID id = world.createDynamicBody(sphere, glm::vec3(0, 5, 0));

    float xBefore = world.getBodyPosition(id).x;

    // Apply a strong horizontal impulse
    world.applyImpulse(id, glm::vec3(100, 0, 0));

    // Step once
    world.update(1.0f / 60.0f);

    float xAfter = world.getBodyPosition(id).x;
    EXPECT_GT(xAfter, xBefore);

    world.shutdown();
}

TEST(PhysicsWorld, DestroyInvalidBodyIsSafe)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    // Destroying an invalid body ID should not crash
    world.destroyBody(JPH::BodyID());

    world.shutdown();
}

// ---------------------------------------------------------------------------
// Coordinate conversion helpers
// ---------------------------------------------------------------------------

TEST(JoltHelpers, Vec3RoundTrip)
{
    glm::vec3 original(1.5f, -2.7f, 3.14f);
    JPH::Vec3 jv = toJolt(original);
    glm::vec3 result = toGlm(jv);

    EXPECT_NEAR(result.x, original.x, 1e-5f);
    EXPECT_NEAR(result.y, original.y, 1e-5f);
    EXPECT_NEAR(result.z, original.z, 1e-5f);
}

TEST(JoltHelpers, QuatRoundTrip)
{
    glm::quat original = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));
    JPH::Quat jq = toJolt(original);
    glm::quat result = toGlm(jq);

    EXPECT_NEAR(result.w, original.w, 1e-5f);
    EXPECT_NEAR(result.x, original.x, 1e-5f);
    EXPECT_NEAR(result.y, original.y, 1e-5f);
    EXPECT_NEAR(result.z, original.z, 1e-5f);
}
