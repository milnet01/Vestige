// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_physics_constraint.cpp
/// @brief Unit tests for the PhysicsConstraint system.
#include "physics/physics_world.h"
#include "physics/physics_constraint.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <gtest/gtest.h>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Test fixture — PhysicsWorld with a static floor and two dynamic boxes
// ---------------------------------------------------------------------------

class PhysicsConstraintTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(world.initialize());

        // Large static floor: top surface at Y=0
        JPH::BoxShape* floor = new JPH::BoxShape(JPH::Vec3(100, 0.5f, 100));
        world.createStaticBody(floor, glm::vec3(0, -0.5f, 0));

        // Two dynamic boxes at known positions
        JPH::BoxShape* box = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        bodyA = world.createDynamicBody(box, glm::vec3(0, 2, 0), glm::quat(1, 0, 0, 0), 1.0f);
        bodyB = world.createDynamicBody(box, glm::vec3(2, 2, 0), glm::quat(1, 0, 0, 0), 1.0f);
    }

    void TearDown() override
    {
        world.shutdown();
    }

    void step(int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
        {
            world.update(1.0f / 60.0f);
        }
    }

    PhysicsWorld world;
    JPH::BodyID bodyA;
    JPH::BodyID bodyB;
};

// ---------------------------------------------------------------------------
// Hinge constraint
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, HingeCreationAndLimits)
{
    glm::vec3 pivot(1.0f, 2.0f, 0.0f);
    glm::vec3 axis(0, 1, 0);
    glm::vec3 normal(1, 0, 0);

    ConstraintHandle handle = world.addHingeConstraint(
        bodyA, bodyB, pivot, axis, normal, -90.0f, 90.0f);

    ASSERT_TRUE(handle.isValid());

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);
    EXPECT_EQ(pc->getType(), ConstraintType::HINGE);
    EXPECT_TRUE(pc->isEnabled());
    EXPECT_EQ(pc->getBodyA(), bodyA);
    EXPECT_EQ(pc->getBodyB(), bodyB);

    // Hinge accessor should work
    EXPECT_NE(pc->asHinge(), nullptr);
    // Wrong type accessors should return nullptr
    EXPECT_EQ(pc->asFixed(), nullptr);
    EXPECT_EQ(pc->asDistance(), nullptr);
}

TEST_F(PhysicsConstraintTest, HingeMotorVelocity)
{
    glm::vec3 pivot(1.0f, 2.0f, 0.0f);
    glm::vec3 axis(0, 1, 0);
    glm::vec3 normal(1, 0, 0);

    ConstraintHandle handle = world.addHingeConstraint(
        bodyA, bodyB, pivot, axis, normal, -180.0f, 180.0f);

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);

    JPH::HingeConstraint* hinge = pc->asHinge();
    ASSERT_NE(hinge, nullptr);

    // Set velocity motor
    hinge->SetMotorState(JPH::EMotorState::Velocity);
    hinge->SetTargetAngularVelocity(1.0f);  // 1 rad/s

    // Simulate — should rotate
    step(60);

    // Verify the motor is still active
    EXPECT_EQ(hinge->GetMotorState(), JPH::EMotorState::Velocity);
}

// ---------------------------------------------------------------------------
// Fixed constraint
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, FixedConstraintWeld)
{
    ConstraintHandle handle = world.addFixedConstraint(bodyA, bodyB);

    ASSERT_TRUE(handle.isValid());

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);
    EXPECT_EQ(pc->getType(), ConstraintType::FIXED);
    EXPECT_NE(pc->asFixed(), nullptr);

    // Record initial distance between bodies
    glm::vec3 posA0 = world.getBodyPosition(bodyA);
    glm::vec3 posB0 = world.getBodyPosition(bodyB);
    float dist0 = glm::length(posA0 - posB0);

    // Simulate — both should fall together maintaining distance
    step(60);

    glm::vec3 posA1 = world.getBodyPosition(bodyA);
    glm::vec3 posB1 = world.getBodyPosition(bodyB);
    float dist1 = glm::length(posA1 - posB1);

    // Distance should be approximately maintained
    EXPECT_NEAR(dist0, dist1, 0.1f);

    // Both should have fallen (gravity)
    EXPECT_LT(posA1.y, posA0.y);
    EXPECT_LT(posB1.y, posB0.y);
}

// ---------------------------------------------------------------------------
// Distance constraint
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, DistanceConstraintRigid)
{
    glm::vec3 ptA(0, 2, 0);
    glm::vec3 ptB(2, 2, 0);

    ConstraintHandle handle = world.addDistanceConstraint(
        bodyA, bodyB, ptA, ptB);

    ASSERT_TRUE(handle.isValid());

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);
    EXPECT_EQ(pc->getType(), ConstraintType::DISTANCE);
    EXPECT_NE(pc->asDistance(), nullptr);

    // Record initial distance
    float initialDist = glm::length(ptA - ptB);

    // Simulate
    step(60);

    glm::vec3 posA = world.getBodyPosition(bodyA);
    glm::vec3 posB = world.getBodyPosition(bodyB);
    float finalDist = glm::length(posA - posB);

    // Distance should be approximately maintained
    EXPECT_NEAR(initialDist, finalDist, 0.3f);
}

TEST_F(PhysicsConstraintTest, DistanceConstraintSpring)
{
    glm::vec3 ptA(0, 2, 0);
    glm::vec3 ptB(2, 2, 0);

    ConstraintHandle handle = world.addDistanceConstraint(
        bodyA, bodyB, ptA, ptB, -1.0f, -1.0f, 2.0f, 0.5f);

    ASSERT_TRUE(handle.isValid());

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);

    // Spring should allow some stretching. Just verify it was created.
    EXPECT_NE(pc->asDistance(), nullptr);
}

// ---------------------------------------------------------------------------
// Point constraint
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, PointConstraintSwing)
{
    glm::vec3 pivot(1.0f, 2.0f, 0.0f);

    ConstraintHandle handle = world.addPointConstraint(bodyA, bodyB, pivot);

    ASSERT_TRUE(handle.isValid());

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);
    EXPECT_EQ(pc->getType(), ConstraintType::POINT);
    EXPECT_NE(pc->asPoint(), nullptr);

    // Simulate — bodies should swing around pivot
    step(60);

    // Both bodies should still be reachable
    EXPECT_NE(world.getConstraint(handle), nullptr);
}

// ---------------------------------------------------------------------------
// Slider constraint
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, SliderConstraintLimits)
{
    glm::vec3 slideAxis(1, 0, 0);

    ConstraintHandle handle = world.addSliderConstraint(
        bodyA, bodyB, slideAxis, -1.0f, 1.0f);

    ASSERT_TRUE(handle.isValid());

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);
    EXPECT_EQ(pc->getType(), ConstraintType::SLIDER);
    EXPECT_NE(pc->asSlider(), nullptr);
}

// ---------------------------------------------------------------------------
// World attachment (body-to-world)
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, WorldAttachment)
{
    // Attach bodyB to the world with a point constraint
    glm::vec3 pivot(2.0f, 2.0f, 0.0f);

    ConstraintHandle handle = world.addPointConstraint(
        JPH::BodyID(), bodyB, pivot);

    ASSERT_TRUE(handle.isValid());

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);

    // BodyA should be invalid (world anchor)
    EXPECT_TRUE(pc->getBodyA().IsInvalid());
    EXPECT_EQ(pc->getBodyB(), bodyB);

    // Simulate — bodyB should stay near the pivot (held by constraint)
    step(120);

    glm::vec3 posB = world.getBodyPosition(bodyB);

    // Should not have fallen far from pivot height (constraint holds it up)
    EXPECT_GT(posB.y, 0.5f);
}

// ---------------------------------------------------------------------------
// Breaking constraints
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, BreakableConstraint)
{
    ConstraintHandle handle = world.addFixedConstraint(bodyA, bodyB);

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);

    // Set a very low break force so gravity alone breaks it
    pc->setBreakForce(0.01f);

    // Simulate — should break quickly due to gravity pulling both bodies
    for (int i = 0; i < 60; ++i)
    {
        world.update(1.0f / 60.0f);
        world.checkBreakableConstraints(1.0f / 60.0f);
    }

    // Constraint should be disabled now
    pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);
    EXPECT_FALSE(pc->isEnabled());
}

TEST_F(PhysicsConstraintTest, UnbreakableConstraintStaysEnabled)
{
    ConstraintHandle handle = world.addFixedConstraint(bodyA, bodyB);

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);
    EXPECT_FLOAT_EQ(pc->getBreakForce(), 0.0f);  // Default = unbreakable

    // Simulate
    for (int i = 0; i < 60; ++i)
    {
        world.update(1.0f / 60.0f);
        world.checkBreakableConstraints(1.0f / 60.0f);
    }

    EXPECT_TRUE(pc->isEnabled());
}

// ---------------------------------------------------------------------------
// Constraint removal
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, RemoveConstraint)
{
    ConstraintHandle handle = world.addFixedConstraint(bodyA, bodyB);
    ASSERT_TRUE(handle.isValid());
    EXPECT_NE(world.getConstraint(handle), nullptr);

    world.removeConstraint(handle);

    // Handle should now be stale
    EXPECT_EQ(world.getConstraint(handle), nullptr);
}

TEST_F(PhysicsConstraintTest, DestroyBodyRemovesConstraints)
{
    ConstraintHandle h1 = world.addFixedConstraint(bodyA, bodyB);
    ConstraintHandle h2 = world.addPointConstraint(bodyA, bodyB, glm::vec3(1, 2, 0));

    ASSERT_NE(world.getConstraint(h1), nullptr);
    ASSERT_NE(world.getConstraint(h2), nullptr);

    // Destroying bodyA should remove both constraints
    world.destroyBody(bodyA);

    EXPECT_EQ(world.getConstraint(h1), nullptr);
    EXPECT_EQ(world.getConstraint(h2), nullptr);
}

// ---------------------------------------------------------------------------
// Handle validation
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, HandleValidation)
{
    // Default handle is invalid
    ConstraintHandle invalid;
    EXPECT_FALSE(invalid.isValid());
    EXPECT_EQ(world.getConstraint(invalid), nullptr);

    // Valid handle works
    ConstraintHandle handle = world.addFixedConstraint(bodyA, bodyB);
    EXPECT_TRUE(handle.isValid());
    EXPECT_NE(world.getConstraint(handle), nullptr);

    // Stale handle (after removal) returns nullptr
    world.removeConstraint(handle);
    EXPECT_EQ(world.getConstraint(handle), nullptr);
}

TEST_F(PhysicsConstraintTest, EnableDisableConstraint)
{
    ConstraintHandle handle = world.addFixedConstraint(bodyA, bodyB);

    PhysicsConstraint* pc = world.getConstraint(handle);
    ASSERT_NE(pc, nullptr);
    EXPECT_TRUE(pc->isEnabled());

    pc->setEnabled(false);
    EXPECT_FALSE(pc->isEnabled());

    pc->setEnabled(true);
    EXPECT_TRUE(pc->isEnabled());
}

// ---------------------------------------------------------------------------
// Raycast
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, RayCastHitsBody)
{
    // Let bodies settle on the floor first
    step(120);

    // Cast ray downward from above bodyA
    glm::vec3 posA = world.getBodyPosition(bodyA);
    glm::vec3 origin = posA + glm::vec3(0, 5, 0);
    glm::vec3 direction(0, -10, 0);

    JPH::BodyID hitBody;
    float fraction = 0.0f;
    bool hit = world.rayCast(origin, direction, hitBody, fraction);

    // Should hit something (either bodyA or the floor)
    EXPECT_TRUE(hit);
    EXPECT_GT(fraction, 0.0f);
    EXPECT_LT(fraction, 1.0f);
}

TEST_F(PhysicsConstraintTest, RayCastMissesInEmptyDirection)
{
    // Cast ray into empty space (high up, pointing further up)
    glm::vec3 origin(0, 100, 0);
    glm::vec3 direction(0, 10, 0);

    JPH::BodyID hitBody;
    float fraction = 0.0f;
    bool hit = world.rayCast(origin, direction, hitBody, fraction);

    EXPECT_FALSE(hit);
}

// ---------------------------------------------------------------------------
// Impulse at point
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, ImpulseAtPointCreatesTorque)
{
    glm::vec3 posBefore = world.getBodyPosition(bodyB);

    // Apply impulse off-center to create spin
    glm::vec3 impulsePoint = posBefore + glm::vec3(0.5f, 0, 0);
    world.applyImpulseAtPoint(bodyB, glm::vec3(0, 0, 5), impulsePoint);

    step(30);

    glm::vec3 posAfter = world.getBodyPosition(bodyB);

    // Body should have moved in Z direction
    EXPECT_NE(posAfter.z, posBefore.z);
}

// ---------------------------------------------------------------------------
// Multiple constraints on same body
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, MultipleConstraintsOnSameBody)
{
    // Create a third body
    JPH::BoxShape* box = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
    JPH::BodyID bodyC = world.createDynamicBody(box, glm::vec3(4, 2, 0));

    ConstraintHandle h1 = world.addFixedConstraint(bodyA, bodyB);
    ConstraintHandle h2 = world.addFixedConstraint(bodyB, bodyC);

    ASSERT_NE(world.getConstraint(h1), nullptr);
    ASSERT_NE(world.getConstraint(h2), nullptr);

    // Destroying bodyB should remove both constraints
    world.destroyBody(bodyB);

    EXPECT_EQ(world.getConstraint(h1), nullptr);
    EXPECT_EQ(world.getConstraint(h2), nullptr);
}

// ---------------------------------------------------------------------------
// Constraint handles list
// ---------------------------------------------------------------------------

TEST_F(PhysicsConstraintTest, GetConstraintHandles)
{
    EXPECT_TRUE(world.getConstraintHandles().empty());

    ConstraintHandle h1 = world.addFixedConstraint(bodyA, bodyB);
    ConstraintHandle h2 = world.addPointConstraint(bodyA, bodyB, glm::vec3(1, 2, 0));

    auto handles = world.getConstraintHandles();
    EXPECT_EQ(handles.size(), 2u);

    world.removeConstraint(h1);
    handles = world.getConstraintHandles();
    EXPECT_EQ(handles.size(), 1u);

    world.removeConstraint(h2);
    handles = world.getConstraintHandles();
    EXPECT_TRUE(handles.empty());
}

// ---------------------------------------------------------------------------
// Phase 10.9 Ph6: Deterministic iteration order
// ---------------------------------------------------------------------------
// Hash-dependent iteration order over m_constraints would let
// break-order tests and Phase 11A replay diverge between runs (or
// across stdlib versions). std::map orders by index, which matches
// insertion order because indices grow monotonically. Pin that the
// reported handles are sorted by index across two independent worlds
// built from the same script.
TEST(PhysicsConstraintIterationOrder, Ph6_DeterministicAcrossWorlds)
{
    auto buildAndCollect = []() {
        PhysicsWorld w;
        EXPECT_TRUE(w.initialize());
        JPH::BoxShape* box = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        JPH::BodyID a = w.createDynamicBody(box, glm::vec3(0, 2, 0));
        JPH::BodyID b = w.createDynamicBody(box, glm::vec3(2, 2, 0));
        JPH::BodyID c = w.createDynamicBody(box, glm::vec3(4, 2, 0));
        // Insert in a fixed sequence — the resulting handle list must
        // come back in the same order every time.
        w.addFixedConstraint(a, b);
        w.addPointConstraint(b, c, glm::vec3(3, 2, 0));
        w.addFixedConstraint(a, c);
        std::vector<uint32_t> indices;
        for (const auto& h : w.getConstraintHandles())
        {
            indices.push_back(h.index);
        }
        w.shutdown();
        return indices;
    };

    const auto run1 = buildAndCollect();
    const auto run2 = buildAndCollect();
    ASSERT_EQ(run1.size(), 3u);
    EXPECT_EQ(run1, run2);
    // Indices must be sorted ascending — that is the determinism contract.
    for (size_t i = 1; i < run1.size(); ++i)
    {
        EXPECT_LT(run1[i - 1], run1[i])
            << "Ph6: constraint iteration must be ordered by index, "
               "not hash bucket";
    }
}

// Ph6 follow-on: removing then adding does not collapse generations
// onto a stale index. Today indices grow monotonically (no reuse), so
// a fresh handle gets a *new* index — confirm the contract.
TEST(PhysicsConstraintIterationOrder, Ph6_NoIndexReuseAfterRemove)
{
    PhysicsWorld w;
    ASSERT_TRUE(w.initialize());
    JPH::BoxShape* box = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
    JPH::BodyID a = w.createDynamicBody(box, glm::vec3(0, 2, 0));
    JPH::BodyID b = w.createDynamicBody(box, glm::vec3(2, 2, 0));

    ConstraintHandle h1 = w.addFixedConstraint(a, b);
    w.removeConstraint(h1);
    ConstraintHandle h2 = w.addFixedConstraint(a, b);

    EXPECT_NE(h1.index, h2.index)
        << "Ph6: indices should grow monotonically — recycling them "
           "without bumping a per-slot generation would let a stale "
           "handle silently resolve to the new constraint";
    // Per-slot generation: every fresh slot starts at 1 (no global
    // counter incrementing across allocations).
    EXPECT_EQ(h2.generation, 1u);

    w.shutdown();
}
