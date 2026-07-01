// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_contact_event.cpp
/// @brief Collision-event bus tests (AX4 S3): a real dropped body produces one
///        Enter CollisionEvent carrying the body pair + point + approach speed;
///        a resting contact does not machine-gun the bus; removing a body
///        emits the paired Exit event.
#include "physics/physics_world.h"
#include "physics/contact_event.h"
#include "physics/rigid_body.h"
#include "core/event_bus.h"
#include "scene/entity.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace Vestige;

namespace
{

/// Drives the world forward @a frames steps at 60 Hz.
void step(PhysicsWorld& world, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        world.update(1.0f / 60.0f);
    }
}

/// Sets @a floor up as a static floor whose top sits at Y=0, tagged Stone.
/// Takes the entity by reference — Entity is non-copyable/non-movable.
void setupFloor(Entity& floor, PhysicsWorld& world)
{
    floor.transform.position = glm::vec3(0.0f, -0.5f, 0.0f);
    auto* rb = floor.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::STATIC;
    rb->shapeType = CollisionShapeType::BOX;
    rb->shapeSize = glm::vec3(50.0f, 0.5f, 50.0f);
    rb->surfaceMaterial = SurfaceMaterial::Stone;
    floor.update(0.0f);
    rb->createBody(world);
}

}  // namespace

TEST(ContactEvent, DroppedBodyFiresOneEnterEvent)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    EventBus bus;
    world.setEventBus(bus);

    std::vector<CollisionEvent> events;
    bus.subscribe<CollisionEvent>([&](const CollisionEvent& e) { events.push_back(e); });

    Entity floor("Floor");
    setupFloor(floor, world);

    Entity ball("Ball");
    ball.transform.position = glm::vec3(0.0f, 3.0f, 0.0f);
    auto* ballRb = ball.addComponent<RigidBody>();
    ballRb->motionType = BodyMotionType::DYNAMIC;
    ballRb->shapeType = CollisionShapeType::BOX;
    ballRb->shapeSize = glm::vec3(0.5f);
    ballRb->surfaceMaterial = SurfaceMaterial::Metal;
    ball.update(0.0f);
    ballRb->createBody(world);

    step(world, 180);  // ~3 s — long enough to fall, hit, and settle.

    ASSERT_FALSE(events.empty()) << "a 3 m drop must produce an impact event";

    // The first Enter must carry the ball/floor pair + a real approach speed.
    const CollisionEvent& first = events.front();
    EXPECT_TRUE(first.isEnter);
    EXPECT_GE(first.approachSpeed, kMinImpactSpeed);

    const EntityId a = first.entityA;
    const EntityId b = first.entityB;
    const bool pairMatches =
        (a == ball.getId() && b == floor.getId()) ||
        (a == floor.getId() && b == ball.getId());
    EXPECT_TRUE(pairMatches) << "event must name the colliding entities";

    // Contact is at the floor surface (Y≈0) with a roughly vertical normal.
    EXPECT_NEAR(first.point.y, 0.0f, 0.6f);
    EXPECT_GT(std::abs(first.normal.y), 0.5f);

    // Threshold + throttle keep the count bounded — a settling box must not
    // machine-gun the bus into hundreds of events.
    const int enterCount = static_cast<int>(
        std::count_if(events.begin(), events.end(),
                      [](const CollisionEvent& e) { return e.isEnter; }));
    EXPECT_LE(enterCount, 10) << "resting/jittering contact must not spam";

    world.shutdown();
}

TEST(ContactEvent, RestingContactDoesNotKeepFiring)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    EventBus bus;
    world.setEventBus(bus);

    int enterCount = 0;
    bus.subscribe<CollisionEvent>([&](const CollisionEvent& e) {
        if (e.isEnter) ++enterCount;
    });

    Entity floor("Floor");
    setupFloor(floor, world);

    Entity box("Box");
    box.transform.position = glm::vec3(0.0f, 0.55f, 0.0f);  // barely above floor
    auto* rb = box.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::DYNAMIC;
    rb->shapeType = CollisionShapeType::BOX;
    rb->shapeSize = glm::vec3(0.5f);
    box.update(0.0f);
    rb->createBody(world);

    step(world, 120);  // 2 s — let it settle.
    const int afterSettle = enterCount;
    step(world, 120);  // 2 s more — fully at rest now.
    const int delta = enterCount - afterSettle;

    EXPECT_LE(delta, 1) << "a body at rest must stop generating Enter events";

    world.shutdown();
}

TEST(ContactEvent, RemovingBodyEmitsExit)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    EventBus bus;
    world.setEventBus(bus);

    std::vector<CollisionEvent> events;
    bus.subscribe<CollisionEvent>([&](const CollisionEvent& e) { events.push_back(e); });

    Entity floor("Floor");
    setupFloor(floor, world);

    Entity ball("Ball");
    ball.transform.position = glm::vec3(0.0f, 0.55f, 0.0f);  // just above the floor
    auto* ballRb = ball.addComponent<RigidBody>();
    ballRb->motionType = BodyMotionType::DYNAMIC;
    ballRb->shapeType = CollisionShapeType::BOX;
    ballRb->shapeSize = glm::vec3(0.5f);
    ballRb->restitution = 0.0f;  // no bounce — keep the contact stable
    ball.update(0.0f);
    ballRb->createBody(world);

    // Step only until the contact is established (it is then freshly cached and
    // active), so the Exit test isn't perturbed by settle/sleep churn.
    bool entered = false;
    for (int i = 0; i < 60 && !entered; ++i)
    {
        world.update(1.0f / 60.0f);
        entered = std::any_of(events.begin(), events.end(),
                              [](const CollisionEvent& e) { return e.isEnter; });
    }
    ASSERT_TRUE(entered) << "precondition: the ball must have entered contact";

    ballRb->destroyBody();  // removes the body → OnContactRemoved next step.
    events.clear();
    step(world, 2);

    ASSERT_FALSE(events.empty()) << "removing a contacting body must emit Exit";
    // cppcheck-suppress containerOutOfBounds  ; FP: events.clear() above then repopulated via the bus callback during step(); ASSERT_FALSE already guards emptiness
    const CollisionEvent& exit = events.front();
    EXPECT_FALSE(exit.isEnter);
    const bool pairMatches =
        (exit.entityA == ball.getId() && exit.entityB == floor.getId()) ||
        (exit.entityA == floor.getId() && exit.entityB == ball.getId());
    EXPECT_TRUE(pairMatches);

    world.shutdown();
}

TEST(ContactEvent, NoEventBusDoesNotCrash)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    // Deliberately no setEventBus — contacts must still drain (bounded) and
    // must not crash.
    Entity floor("Floor");
    setupFloor(floor, world);

    Entity ball("Ball");
    ball.transform.position = glm::vec3(0.0f, 2.0f, 0.0f);
    auto* ballRb = ball.addComponent<RigidBody>();
    ballRb->motionType = BodyMotionType::DYNAMIC;
    ballRb->shapeType = CollisionShapeType::BOX;
    ballRb->shapeSize = glm::vec3(0.5f);
    ball.update(0.0f);
    ballRb->createBody(world);

    step(world, 120);  // must not crash with no bus attached.
    SUCCEED();

    world.shutdown();
}
