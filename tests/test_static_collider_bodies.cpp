// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_static_collider_bodies.cpp
/// @brief Jolt's static boxes follow the scene's colliders (3D_E-0796).

#include <gtest/gtest.h>

#include "physics/physics_world.h"
#include "physics/static_collider_bodies.h"

using namespace Vestige;

namespace
{
AABB box(float x)
{
    return AABB::fromCenterSize(glm::vec3(x, 1.0f, 0.0f), glm::vec3(1.0f));
}

unsigned bodyCount(const PhysicsWorld& world)
{
    return world.getSystem()->GetNumBodies();
}
} // namespace

TEST(StaticColliderBodies, HashChangesOnEveryKindOfEdit)
{
    const std::vector<AABB> scene = {box(0.0f), box(5.0f)};
    EXPECT_EQ(hashColliders(scene), hashColliders({box(0.0f), box(5.0f)}));
    EXPECT_NE(hashColliders(scene), hashColliders({box(0.0f), box(5.0f), box(9.0f)}));  // add
    EXPECT_NE(hashColliders(scene), hashColliders({box(0.0f)}));                          // delete
    EXPECT_NE(hashColliders(scene), hashColliders({box(0.0f), box(5.5f)}));               // move
    EXPECT_NE(hashColliders(scene), hashColliders({}));                                   // new empty scene
}

TEST(StaticColliderBodies, BoxesFollowAnEditOnceItSettles)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    StaticColliderBodies statics;

    statics.rebuild(world, {box(0.0f), box(5.0f)});
    ASSERT_EQ(statics.bodies().size(), 2u);
    EXPECT_EQ(bodyCount(world), 2u);

    // Nothing changed: no rebuild.
    EXPECT_FALSE(statics.update(world, {box(0.0f), box(5.0f)}));

    // A prop is dragged over two frames, then let go: one rebuild, after it settles.
    EXPECT_FALSE(statics.update(world, {box(0.0f), box(6.0f)}));
    EXPECT_FALSE(statics.update(world, {box(0.0f), box(7.0f)}));
    EXPECT_TRUE(statics.update(world, {box(0.0f), box(7.0f)}));
    ASSERT_EQ(statics.bodies().size(), 2u);
    EXPECT_NEAR(world.getBodyPosition(statics.bodies()[1]).x, 7.0f, 0.001f);
    EXPECT_EQ(bodyCount(world), 2u) << "the old boxes must be removed, not kept";

    // A prop is deleted.
    EXPECT_FALSE(statics.update(world, {box(0.0f)}));
    EXPECT_TRUE(statics.update(world, {box(0.0f)}));
    EXPECT_EQ(bodyCount(world), 1u);

    // Another scene is opened, with no blocking props at all.
    EXPECT_FALSE(statics.update(world, {}));
    EXPECT_TRUE(statics.update(world, {}));
    EXPECT_EQ(bodyCount(world), 0u);

    world.shutdown();
}

TEST(StaticColliderBodies, ThinBoxesAreSkipped)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    StaticColliderBodies statics;

    const AABB flat = AABB::fromCenterSize(glm::vec3(0.0f), glm::vec3(2.0f, 0.05f, 2.0f));
    statics.rebuild(world, {flat, box(3.0f)});
    EXPECT_EQ(statics.bodies().size(), 1u);

    statics.clear(world);
    world.shutdown();
}
