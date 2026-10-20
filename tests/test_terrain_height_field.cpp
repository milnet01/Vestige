// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_height_field.cpp
/// @brief Tests for createTerrainHeightFieldShape — the Jolt collider that
///        keeps the physics walker on raised terrain (3D_E-0757).
#include "physics/terrain_height_field.h"
#include "physics/physics_character_controller.h"
#include "physics/physics_world.h"
#include "physics_test_helpers.h"

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{

// No Y=0 floor: the height field must hold the character up on its own.
class TerrainHeightFieldTest : public ::Vestige::Testing::PhysicsWorldFixture
{
protected:
    bool wantFloor() const override { return false; }
};

constexpr int SIDE = 33;
constexpr float HEIGHT_SCALE = 50.0f;

// A SIDE x SIDE grid centred on the world origin, 1 m apart.
TerrainHeightFieldDesc makeDesc(float (*height01)(int x, int z))
{
    TerrainHeightFieldDesc desc;
    desc.sampleCount = SIDE;
    desc.origin = glm::vec3(-16.0f, 0.0f, -16.0f);
    desc.heightScale = HEIGHT_SCALE;
    desc.heights01.resize(static_cast<size_t>(SIDE) * SIDE);
    for (int z = 0; z < SIDE; ++z)
    {
        for (int x = 0; x < SIDE; ++x)
        {
            desc.heights01[static_cast<size_t>(z) * SIDE + x] = height01(x, z);
        }
    }
    return desc;
}

// 0.14 * 50 = 7 m, the meadow's base height.
float flatMeadow01(int /*x*/, int /*z*/) { return 0.14f; }

// Rises 0.5 m per metre along X, from 5 m at the west edge.
float rampAlongX01(int x, int /*z*/) { return (5.0f + 0.5f * static_cast<float>(x)) / HEIGHT_SCALE; }

} // namespace

TEST(TerrainHeightField, MalformedDescGivesNoShape)
{
    TerrainHeightFieldDesc empty;
    EXPECT_EQ(createTerrainHeightFieldShape(empty), nullptr);

    TerrainHeightFieldDesc wrongCount = makeDesc(flatMeadow01);
    wrongCount.heights01.pop_back();
    EXPECT_EQ(createTerrainHeightFieldShape(wrongCount), nullptr);
}

TEST_F(TerrainHeightFieldTest, WalkerSettlesOnRaisedTerrain)
{
    JPH::ShapeRefC shape = createTerrainHeightFieldShape(makeDesc(flatMeadow01));
    ASSERT_NE(shape, nullptr);
    world.createStaticBody(shape.GetPtr(), glm::vec3(0.0f));

    PhysicsControllerConfig config;
    config.gravity = glm::vec3(0, -9.81f, 0);
    PhysicsCharacterController ctrl;
    ASSERT_TRUE(ctrl.initialize(world, glm::vec3(0, 10, 0), config));

    for (int i = 0; i < 180; ++i)
    {
        ctrl.update(1.0f / 60.0f, glm::vec3(0.0f));
    }

    EXPECT_NEAR(ctrl.getPosition().y, 7.0f, 0.1f);
    EXPECT_TRUE(ctrl.isOnGround());
}

TEST_F(TerrainHeightFieldTest, SurfaceMatchesTheHeightmapOnASlope)
{
    JPH::ShapeRefC shape = createTerrainHeightFieldShape(makeDesc(rampAlongX01));
    ASSERT_NE(shape, nullptr);
    world.createStaticBody(shape.GetPtr(), glm::vec3(0.0f));

    // Sample x = 10 is world X -6 and stands at 5 + 0.5 * 10 = 10 m.
    // Halfway to the next sample (X -5.5) the surface is at 10.25 m.
    for (const glm::vec2 xy : {glm::vec2(-6.0f, 10.0f), glm::vec2(-5.5f, 10.25f)})
    {
        JPH::BodyID hitBody;
        float hitDistance = 0.0f;
        ASSERT_TRUE(world.rayCast(glm::vec3(xy.x, 100.0f, 3.0f), glm::vec3(0, -1, 0),
                                  200.0f, hitBody, hitDistance))
            << "x " << xy.x;
        EXPECT_NEAR(100.0f - hitDistance, xy.y, 0.05f) << "x " << xy.x;
    }
}
