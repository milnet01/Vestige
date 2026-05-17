// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics_test_helpers.h
/// @brief Shared PhysicsWorld scaffolding for tests that need a fixture.
///
/// /test-audit 2026-05-17 Ts19-D4: the `world.initialize()` /
/// `world.shutdown()` + 100×100 static-floor boilerplate appeared in
/// test_physics_constraint.cpp and test_physics_character_controller.cpp.
/// Centralised here so a fixture change updates both at once. Tests in
/// test_physics_world.cpp deliberately construct a fresh PhysicsWorld
/// per case because they're exercising the lifecycle directly — those
/// are intentionally NOT converted.
#pragma once

#include "physics/physics_world.h"

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

namespace Vestige::Testing
{

/// @brief Test fixture providing an initialised PhysicsWorld plus an
///        optional shared static floor whose top surface sits at Y=0.
class PhysicsWorldFixture : public ::testing::Test
{
protected:
    /// Override in subclasses to skip the auto floor (returns false).
    virtual bool wantFloor() const { return true; }

    void SetUp() override
    {
        ASSERT_TRUE(world.initialize());
        if (wantFloor())
        {
            JPH::BoxShape* floor = new JPH::BoxShape(JPH::Vec3(100, 0.5f, 100));
            floorId = world.createStaticBody(floor, glm::vec3(0, -0.5f, 0));
        }
    }

    void TearDown() override
    {
        world.shutdown();
    }

    PhysicsWorld world;
    JPH::BodyID floorId;
};

}  // namespace Vestige::Testing
