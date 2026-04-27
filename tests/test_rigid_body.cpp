// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_rigid_body.cpp
/// @brief Unit tests for the RigidBody component.
#include "physics/rigid_body.h"
#include "physics/physics_world.h"
#include "scene/entity.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <gtest/gtest.h>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Basic creation
// ---------------------------------------------------------------------------

TEST(RigidBody, DefaultState)
{
    RigidBody rb;
    EXPECT_EQ(rb.motionType, BodyMotionType::STATIC);
    EXPECT_EQ(rb.shapeType, CollisionShapeType::BOX);
    EXPECT_FALSE(rb.hasBody());
}

TEST(RigidBody, CreateStaticBoxBody)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("TestBox");
    entity.transform.position = glm::vec3(1, 2, 3);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::STATIC;
    rb->shapeType = CollisionShapeType::BOX;
    rb->shapeSize = glm::vec3(0.5f);

    // Need to do one update so the world matrix is computed
    entity.update(0.0f);

    rb->createBody(world);
    EXPECT_TRUE(rb->hasBody());

    glm::vec3 pos = world.getBodyPosition(rb->getBodyId());
    EXPECT_NEAR(pos.x, 1.0f, 0.01f);
    EXPECT_NEAR(pos.y, 2.0f, 0.01f);
    EXPECT_NEAR(pos.z, 3.0f, 0.01f);

    rb->destroyBody();
    EXPECT_FALSE(rb->hasBody());

    world.shutdown();
}

TEST(RigidBody, CreateDynamicSphereBody)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("TestSphere");
    entity.transform.position = glm::vec3(0, 10, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::DYNAMIC;
    rb->shapeType = CollisionShapeType::SPHERE;
    rb->shapeSize = glm::vec3(0.5f);
    rb->mass = 2.0f;

    rb->createBody(world);
    EXPECT_TRUE(rb->hasBody());
    EXPECT_EQ(world.getActiveBodyCount(), 1u);

    world.shutdown();
}

TEST(RigidBody, CreateCapsuleBody)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("TestCapsule");
    entity.transform.position = glm::vec3(0, 5, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::DYNAMIC;
    rb->shapeType = CollisionShapeType::CAPSULE;
    rb->shapeSize = glm::vec3(0.3f, 0.55f, 0.0f);  // radius, halfHeight

    rb->createBody(world);
    EXPECT_TRUE(rb->hasBody());

    world.shutdown();
}

// ---------------------------------------------------------------------------
// Dynamic body sync
// ---------------------------------------------------------------------------

TEST(RigidBody, DynamicBodySyncsToTransform)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    // Create floor
    JPH::BoxShape* floor = new JPH::BoxShape(JPH::Vec3(50, 0.5f, 50));
    world.createStaticBody(floor, glm::vec3(0, -0.5f, 0));

    Entity entity("FallingBox");
    entity.transform.position = glm::vec3(0, 5, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::DYNAMIC;
    rb->shapeType = CollisionShapeType::BOX;
    rb->shapeSize = glm::vec3(0.25f);
    rb->mass = 1.0f;

    rb->createBody(world);

    float initialY = entity.transform.position.y;

    // Step physics and sync
    for (int i = 0; i < 30; ++i)
    {
        world.update(1.0f / 60.0f);
        rb->syncTransform();
    }

    // Entity transform should have been updated by physics
    EXPECT_LT(entity.transform.position.y, initialY);

    world.shutdown();
}

// Phase 10.9 Ph9: dynamic-body sync must preserve orientation past
// ±90° pitch. The previous code path round-tripped through
// `glm::eulerAngles`, which has a singularity at ±π/2 pitch — a
// tumbling body's quaternion would lose information frame-over-frame.
// The new code writes the quaternion-derived matrix into the Transform's
// matrix override, so the rendered orientation matches the physics
// orientation exactly even at the gimbal-lock pitch.
TEST(RigidBody, DynamicSyncSetsMatrixOverrideQuaternionExact_Ph9)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("Tumbler");
    entity.transform.position = glm::vec3(0, 5, 0);
    // Pitch the body to exactly +90° (the Euler singularity).
    const glm::quat targetRot = glm::angleAxis(
        glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    entity.transform.rotation = glm::eulerAngles(targetRot);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::DYNAMIC;
    rb->shapeType = CollisionShapeType::BOX;
    rb->shapeSize = glm::vec3(0.25f);
    rb->mass = 1.0f;
    rb->createBody(world);

    // Push the body to the target orientation directly via the world.
    world.setBodyTransform(rb->getBodyId(), glm::vec3(0, 5, 0), targetRot);

    rb->syncTransform();

    // After Ph9, the local matrix carries the exact quaternion — the
    // override is set, and decomposing it should recover targetRot
    // within tight float tolerance, regardless of the Euler singularity.
    ASSERT_TRUE(entity.transform.hasMatrixOverride())
        << "Ph9: dynamic-body sync must populate the matrix override";
    const glm::mat4 m = entity.transform.getLocalMatrix();
    const glm::quat decoded = glm::quat_cast(m);
    // Quaternions q and -q represent the same rotation. Compare via dot.
    const float d = std::abs(glm::dot(decoded, targetRot));
    EXPECT_NEAR(d, 1.0f, 1e-4f)
        << "matrix-override must encode the physics quaternion exactly "
           "(no Euler round-trip)";

    world.shutdown();
}

// ---------------------------------------------------------------------------
// Kinematic body sync
// ---------------------------------------------------------------------------

TEST(RigidBody, KinematicBodySyncsFromTransform)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("Platform");
    entity.transform.position = glm::vec3(0, 0, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::KINEMATIC;
    rb->shapeType = CollisionShapeType::BOX;
    rb->shapeSize = glm::vec3(2, 0.1f, 2);

    rb->createBody(world);

    // Move the entity transform and recompute world matrix
    entity.transform.position = glm::vec3(10, 5, -3);
    entity.update(0.0f);

    // Sync should push the new position to physics
    rb->syncTransform();

    glm::vec3 physPos = world.getBodyPosition(rb->getBodyId());
    EXPECT_NEAR(physPos.x, 10.0f, 0.01f);
    EXPECT_NEAR(physPos.y, 5.0f, 0.01f);
    EXPECT_NEAR(physPos.z, -3.0f, 0.01f);

    world.shutdown();
}

// ---------------------------------------------------------------------------
// Clone
// ---------------------------------------------------------------------------

TEST(RigidBody, ClonePreservesConfiguration)
{
    RigidBody rb;
    rb.motionType = BodyMotionType::DYNAMIC;
    rb.shapeType = CollisionShapeType::SPHERE;
    rb.shapeSize = glm::vec3(1.5f);
    rb.mass = 5.0f;
    rb.friction = 0.8f;
    rb.restitution = 0.2f;

    auto cloned = rb.clone();
    auto* rbClone = static_cast<RigidBody*>(cloned.get());

    EXPECT_EQ(rbClone->motionType, BodyMotionType::DYNAMIC);
    EXPECT_EQ(rbClone->shapeType, CollisionShapeType::SPHERE);
    EXPECT_NEAR(rbClone->shapeSize.x, 1.5f, 0.001f);
    EXPECT_NEAR(rbClone->mass, 5.0f, 0.001f);
    EXPECT_NEAR(rbClone->friction, 0.8f, 0.001f);
    EXPECT_NEAR(rbClone->restitution, 0.2f, 0.001f);

    // Clone should not have a body
    EXPECT_FALSE(rbClone->hasBody());
}

// ---------------------------------------------------------------------------
// Force and impulse
// ---------------------------------------------------------------------------

TEST(RigidBody, ForceOnlyAffectsDynamic)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("StaticBox");
    entity.transform.position = glm::vec3(0, 0, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::STATIC;
    rb->shapeType = CollisionShapeType::BOX;
    rb->shapeSize = glm::vec3(0.5f);

    rb->createBody(world);

    // Force on a static body should be a no-op (no crash)
    rb->addForce(glm::vec3(1000, 0, 0));
    rb->addImpulse(glm::vec3(1000, 0, 0));

    world.update(1.0f / 60.0f);
    rb->syncTransform();

    EXPECT_NEAR(entity.transform.position.x, 0.0f, 0.01f);

    world.shutdown();
}
