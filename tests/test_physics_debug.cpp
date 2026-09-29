// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_physics_debug.cpp
/// @brief PhysicsDebugDraw queues collider wireframes only when enabled
///        (3D_E-0640).
///
/// The editor's View > Physics Colliders toggle enables PhysicsDebugDraw,
/// whose draw() queues lines into DebugDraw for the editor overlay pass to
/// flush. The queue is static and only a GL flush clears it, so each case
/// measures how many vertices draw() added rather than the queue's total.

#include "physics_test_helpers.h"
#include "physics/physics_debug.h"
#include "renderer/debug_draw.h"

#include <gtest/gtest.h>

namespace Vestige::Test
{

using PhysicsDebugDrawTest = Vestige::Testing::PhysicsWorldFixture;

// The fixture's world holds one static body: the floor box. A wire box is
// 12 edges of 2 vertices each.
TEST_F(PhysicsDebugDrawTest, EnabledQueuesOneWireBoxPerStaticBody)
{
    PhysicsDebugDraw physicsDebug;
    physicsDebug.setEnabled(true);

    const size_t before = DebugDraw::getQueuedVertexCount();
    physicsDebug.draw(world);

    EXPECT_EQ(DebugDraw::getQueuedVertexCount() - before, 24u);
}

TEST_F(PhysicsDebugDrawTest, DisabledQueuesNothing)
{
    PhysicsDebugDraw physicsDebug;

    const size_t before = DebugDraw::getQueuedVertexCount();
    physicsDebug.draw(world);

    EXPECT_EQ(DebugDraw::getQueuedVertexCount(), before);
}

} // namespace Vestige::Test
