// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_physics2d_system.cpp
/// @brief Unit tests for Physics2DSystem + RigidBody2DComponent +
/// Collider2DComponent (Phase 9F-2).
///
/// Each test spins up a standalone PhysicsWorld, injects it into the
/// system via `setPhysicsWorldForTesting`, builds a handful of entities
/// with 2D rigid bodies + colliders, and validates the resulting Jolt
/// simulation behaves 2D (Z locked, planar motion, gravity falls).
#include "physics/physics_world.h"
#include "scene/collider_2d_component.h"
#include "scene/entity.h"
#include "scene/rigid_body_2d_component.h"
#include "systems/physics2d_system.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

namespace
{

struct PhysicsFixture
{
    PhysicsWorld world;
    Physics2DSystem system;

    PhysicsFixture()
    {
        world.initialize();
        system.setPhysicsWorldForTesting(&world);
    }

    ~PhysicsFixture()
    {
        world.shutdown();
    }

    void step(float dt = 1.0f / 60.0f, int substeps = 1)
    {
        for (int i = 0; i < substeps; ++i)
        {
            world.update(dt);
        }
    }
};

std::unique_ptr<Entity> makeDynamic(const glm::vec2& pos,
                                    ColliderShape2D shape = ColliderShape2D::Box)
{
    auto e = std::make_unique<Entity>("Dynamic");
    e->transform.position = glm::vec3(pos, 0.0f);
    auto* rb = e->addComponent<RigidBody2DComponent>();
    rb->type = BodyType2D::Dynamic;
    rb->mass = 1.0f;
    auto* cc = e->addComponent<Collider2DComponent>();
    cc->shape = shape;
    cc->halfExtents = glm::vec2(0.5f);
    cc->radius = 0.5f;
    return e;
}

std::unique_ptr<Entity> makeStaticFloor(const glm::vec2& pos, const glm::vec2& half)
{
    auto e = std::make_unique<Entity>("Floor");
    e->transform.position = glm::vec3(pos, 0.0f);
    auto* rb = e->addComponent<RigidBody2DComponent>();
    rb->type = BodyType2D::Static;
    auto* cc = e->addComponent<Collider2DComponent>();
    cc->shape = ColliderShape2D::Box;
    cc->halfExtents = half;
    return e;
}

} // namespace

TEST(Physics2DSystem, InitializeWithSharedWorld)
{
    PhysicsFixture fx;
    EXPECT_TRUE(fx.system.isInitialized());
    EXPECT_EQ(fx.system.liveBodyCount(), 0u);
}

TEST(Physics2DSystem, CreatesDynamicBody)
{
    PhysicsFixture fx;
    auto e = makeDynamic({0.0f, 10.0f});
    const auto id = fx.system.ensureBody(*e);
    EXPECT_FALSE(id.IsInvalid());
    EXPECT_EQ(fx.system.liveBodyCount(), 1u);

    auto* rb = e->getComponent<RigidBody2DComponent>();
    ASSERT_NE(rb, nullptr);
    EXPECT_FALSE(rb->bodyId.IsInvalid());
    EXPECT_EQ(rb->bodyId, id);
}

TEST(Physics2DSystem, EnsureBodyIsIdempotent)
{
    PhysicsFixture fx;
    auto e = makeDynamic({0.0f, 0.0f});
    const auto id1 = fx.system.ensureBody(*e);
    const auto id2 = fx.system.ensureBody(*e);
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(fx.system.liveBodyCount(), 1u);
}

TEST(Physics2DSystem, GravityFallsDynamicBody)
{
    PhysicsFixture fx;
    auto e = makeDynamic({0.0f, 10.0f});
    fx.system.ensureBody(*e);

    // Step 1 second at 60 Hz. Free-fall: y = 0.5 * g * t^2 (g ≈ 9.81).
    // Jolt's default gravity vector is (0, -9.81, 0).
    for (int i = 0; i < 60; ++i) { fx.step(1.0f / 60.0f); }

    auto v = fx.system.getLinearVelocity(*e);
    EXPECT_LE(v.y, -9.0f);       // fell a full g for ~1s
    EXPECT_GE(v.y, -10.5f);      // within tolerance
    EXPECT_NEAR(v.x, 0.0f, 0.01f);
}

TEST(Physics2DSystem, Plane2DLocksZTranslation)
{
    PhysicsFixture fx;
    auto e = makeDynamic({0.0f, 10.0f});
    fx.system.ensureBody(*e);

    // Add a Z impulse — the Plane2D DOF lock should absorb it.
    auto* rb = e->getComponent<RigidBody2DComponent>();
    fx.world.applyImpulse(rb->bodyId, glm::vec3(0.0f, 0.0f, 50.0f));
    for (int i = 0; i < 60; ++i) { fx.step(1.0f / 60.0f); }

    auto pos = fx.world.getBodyPosition(rb->bodyId);
    EXPECT_NEAR(pos.z, 0.0f, 0.01f) << "Plane2D DOF did not lock Z translation";
}

TEST(Physics2DSystem, DynamicRestsOnStaticFloor)
{
    PhysicsFixture fx;
    auto floor = makeStaticFloor({0.0f, -1.0f}, {5.0f, 0.5f});
    auto ball  = makeDynamic({0.0f, 3.0f}, ColliderShape2D::Circle);

    fx.system.ensureBody(*floor);
    fx.system.ensureBody(*ball);

    for (int i = 0; i < 120; ++i) { fx.step(1.0f / 60.0f); }

    const auto* rb = ball->getComponent<RigidBody2DComponent>();
    auto pos = fx.world.getBodyPosition(rb->bodyId);
    // Ball should have settled above the floor (y > -0.5 - radius approx).
    EXPECT_GT(pos.y, -1.0f);
    EXPECT_LT(pos.y, 3.0f);
    // And nearly stationary.
    auto v = fx.system.getLinearVelocity(*ball);
    EXPECT_LT(std::abs(v.y), 1.5f);
}

TEST(Physics2DSystem, ApplyImpulseMovesInXY)
{
    PhysicsFixture fx;
    auto e = makeDynamic({0.0f, 0.0f});
    auto* rb = e->getComponent<RigidBody2DComponent>();
    rb->gravityScale = 0.0f;  // Float in space — easier to isolate impulse.
    fx.system.ensureBody(*e);

    fx.system.applyImpulse(*e, glm::vec2(5.0f, 0.0f));
    fx.step(1.0f / 60.0f, 5);
    auto v = fx.system.getLinearVelocity(*e);
    EXPECT_GT(v.x, 3.0f);
    EXPECT_NEAR(v.y, 0.0f, 0.1f);
}

TEST(Physics2DSystem, SetLinearVelocityPicksUp)
{
    PhysicsFixture fx;
    auto e = makeDynamic({0.0f, 0.0f});
    auto* rb = e->getComponent<RigidBody2DComponent>();
    rb->gravityScale = 0.0f;
    fx.system.ensureBody(*e);

    fx.system.setLinearVelocity(*e, glm::vec2(2.0f, -1.0f));
    fx.step(1.0f / 60.0f);
    auto v = fx.system.getLinearVelocity(*e);
    EXPECT_NEAR(v.x, 2.0f, 0.05f);
    EXPECT_NEAR(v.y, -1.0f, 0.05f);
}

TEST(Physics2DSystem, RemoveBodyCleansUp)
{
    PhysicsFixture fx;
    auto e = makeDynamic({0.0f, 0.0f});
    const auto id = fx.system.ensureBody(*e);
    ASSERT_FALSE(id.IsInvalid());

    fx.system.removeBody(*e);
    EXPECT_EQ(fx.system.liveBodyCount(), 0u);
    const auto* rb = e->getComponent<RigidBody2DComponent>();
    EXPECT_TRUE(rb->bodyId.IsInvalid());

    // Double-remove is safe.
    fx.system.removeBody(*e);
    EXPECT_EQ(fx.system.liveBodyCount(), 0u);
}

TEST(Physics2DSystem, PolygonShapeAccepted)
{
    PhysicsFixture fx;
    auto e = std::make_unique<Entity>("Poly");
    e->transform.position = glm::vec3(0, 5, 0);
    auto* rb = e->addComponent<RigidBody2DComponent>();
    rb->type = BodyType2D::Dynamic;
    auto* cc = e->addComponent<Collider2DComponent>();
    cc->shape = ColliderShape2D::Polygon;
    cc->vertices = {
        {-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}
    };
    EXPECT_FALSE(fx.system.ensureBody(*e).IsInvalid());
}

TEST(Physics2DSystem, DegeneratePolygonRejected)
{
    PhysicsFixture fx;
    auto e = std::make_unique<Entity>("BadPoly");
    e->addComponent<RigidBody2DComponent>();
    auto* cc = e->addComponent<Collider2DComponent>();
    cc->shape = ColliderShape2D::Polygon;
    cc->vertices = {{0, 0}, {1, 0}};  // only 2 vertices — not a polygon
    const auto id = fx.system.ensureBody(*e);
    EXPECT_TRUE(id.IsInvalid());
    EXPECT_EQ(fx.system.liveBodyCount(), 0u);
}

TEST(Physics2DSystem, EdgeChainAcceptedAsStatic)
{
    PhysicsFixture fx;
    auto e = std::make_unique<Entity>("EdgeChain");
    auto* rb = e->addComponent<RigidBody2DComponent>();
    rb->type = BodyType2D::Static;
    auto* cc = e->addComponent<Collider2DComponent>();
    cc->shape = ColliderShape2D::EdgeChain;
    cc->vertices = {{-5, 0}, {0, 1}, {5, 0}};
    EXPECT_FALSE(fx.system.ensureBody(*e).IsInvalid());
}

TEST(Physics2DSystem, FixedRotationDisablesZRotation)
{
    PhysicsFixture fx;
    auto e = makeDynamic({0.0f, 10.0f});
    auto* rb = e->getComponent<RigidBody2DComponent>();
    rb->fixedRotation = true;
    fx.system.ensureBody(*e);

    // Apply an angular impulse (world.applyImpulse doesn't do torque —
    // use body interface directly).
    auto& bi = fx.world.getBodyInterface();
    bi.AddAngularImpulse(rb->bodyId, JPH::Vec3(0.0f, 0.0f, 10.0f));

    for (int i = 0; i < 30; ++i) { fx.step(1.0f / 60.0f); }
    auto rot = fx.world.getBodyRotation(rb->bodyId);
    const float angle = std::acos(std::max(-1.0f, std::min(1.0f, rot.w))) * 2.0f;
    EXPECT_LT(angle, 0.05f) << "Fixed-rotation body rotated despite lock";
}

TEST(Physics2DSystem, SensorDoesNotResolveContact)
{
    PhysicsFixture fx;
    auto floor = makeStaticFloor({0.0f, -1.0f}, {5.0f, 0.5f});
    auto ball  = makeDynamic({0.0f, 3.0f}, ColliderShape2D::Circle);
    auto* cc = ball->getComponent<Collider2DComponent>();
    cc->isSensor = true;
    cc->radius = 0.5f;

    fx.system.ensureBody(*floor);
    fx.system.ensureBody(*ball);

    // Sensors still fall under gravity but pass through geometry.
    for (int i = 0; i < 120; ++i) { fx.step(1.0f / 60.0f); }
    const auto* rb = ball->getComponent<RigidBody2DComponent>();
    auto pos = fx.world.getBodyPosition(rb->bodyId);
    EXPECT_LT(pos.y, -1.0f) << "Sensor did not pass through static floor";
}

TEST(Physics2DSystem, UpdateCachesVelocityOnComponent)
{
    // Smoke test for the ISystem::update pass that reads velocities from
    // Jolt back into RigidBody2DComponent. Without an Engine-driven
    // Scene, invoke the update path through a minimal stub (no scene →
    // update is a no-op, so this test just verifies the early-return
    // branch doesn't crash).
    PhysicsFixture fx;
    fx.system.update(0.016f);  // no Engine, no scene — safe no-op
    SUCCEED();
}
