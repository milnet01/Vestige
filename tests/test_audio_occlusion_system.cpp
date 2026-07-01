// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_occlusion_system.cpp
/// @brief Unit tests for the AX1 S2 occlusion driver seam: the single-ray
///        geometric measurement (`measureOcclusion`) against a real
///        PhysicsWorld, and the temporal smoothing (`slewOcclusionFraction`).
///
/// The full `AudioOcclusionSystem::update()` needs a live Engine (camera +
/// scene + physics), which requires a window/GL context and is not
/// unit-testable here — so the risky logic (ray → binary fraction → material,
/// and the click-free slew) is factored into these two free functions and
/// tested directly, mirroring how the physics tests drive `PhysicsWorld`
/// without an Engine.
#include "systems/audio_occlusion_system.h"

#include "audio/audio_occlusion.h"
#include "audio/occlusion_material_map.h"
#include "physics/physics_world.h"
#include "physics/surface_material.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{

// A tagged static box the ray can hit. Half-extents `he`, surface `mat`.
JPH::BodyID makeWall(PhysicsWorld& world, const glm::vec3& pos,
                     const glm::vec3& he, SurfaceMaterial mat)
{
    JPH::BoxShape* shape = new JPH::BoxShape(JPH::Vec3(he.x, he.y, he.z));
    const JPH::BodyID id = world.createStaticBody(shape, pos);
    world.setBodyTags(id, 0u, mat);
    return id;
}

} // namespace

// A wall on the listener→source line blocks (target fraction 1) and the picked
// material is the blocking body's surface. `ignoreBody` defaults to {}, so this
// also covers "a source with no physics body still casts".
TEST(AudioOcclusionSystem, WallBetweenBlocksAndPicksMaterial)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    const glm::vec3 listener(0.0f, 0.0f, 0.0f);
    const glm::vec3 source(0.0f, 0.0f, 10.0f);
    makeWall(world, glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(2.0f, 2.0f, 0.5f),
             SurfaceMaterial::Stone);

    const OcclusionMeasurement m = measureOcclusion(world, listener, source);
    EXPECT_TRUE(m.blocked);
    EXPECT_FLOAT_EQ(m.targetFraction, 1.0f);
    EXPECT_EQ(m.material, occlusionPresetForSurface(SurfaceMaterial::Stone));

    world.shutdown();
}

// Clear line of sight: no geometry between → unoccluded, material left Air.
TEST(AudioOcclusionSystem, ClearLineOfSightIsUnoccluded)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    const OcclusionMeasurement m = measureOcclusion(
        world, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 10.0f));
    EXPECT_FALSE(m.blocked);
    EXPECT_FLOAT_EQ(m.targetFraction, 0.0f);
    EXPECT_EQ(m.material, AudioOcclusionMaterialPreset::Air);

    world.shutdown();
}

// Coincident listener/source (zero-length direction) → no cast, unoccluded,
// even when both sit inside solid geometry. Proves the degenerate guard runs
// before any raycast (never normalises a zero vector).
TEST(AudioOcclusionSystem, CoincidentSourceListenerSkipsCast)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    const glm::vec3 here(1.0f, 2.0f, 3.0f);
    makeWall(world, here, glm::vec3(1.0f), SurfaceMaterial::Metal);  // engulfs the point

    const OcclusionMeasurement m = measureOcclusion(world, here, here);
    EXPECT_FALSE(m.blocked);
    EXPECT_FLOAT_EQ(m.targetFraction, 0.0f);

    world.shutdown();
}

// A source embedded in its own collider must not occlude itself: passing that
// body as `ignoreBody` skips it, so with no other geometry the path is clear.
TEST(AudioOcclusionSystem, SourceOwnBodyIsIgnored)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    const glm::vec3 listener(0.0f);
    const glm::vec3 source(0.0f, 0.0f, 10.0f);
    // The source's own body straddles the source point (spans z 9.5..10.5).
    const JPH::BodyID ownBody = makeWall(
        world, source, glm::vec3(0.5f), SurfaceMaterial::Wood);

    // Without the filter the source occludes itself...
    EXPECT_TRUE(measureOcclusion(world, listener, source).blocked);
    // ...but excluding its own body clears the path.
    EXPECT_FALSE(measureOcclusion(world, listener, source, ownBody).blocked);

    world.shutdown();
}

// The slew is a plain lerp: it eases toward the target, snaps at amount 1, and
// holds at amount 0 — the click-free smoothing contract.
TEST(AudioOcclusionSystem, SlewEasesTowardTarget)
{
    EXPECT_FLOAT_EQ(slewOcclusionFraction(0.0f, 1.0f, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(slewOcclusionFraction(1.0f, 0.0f, 0.5f), 0.5f);   // release
    EXPECT_FLOAT_EQ(slewOcclusionFraction(0.0f, 1.0f, 1.0f), 1.0f);   // snap
    EXPECT_FLOAT_EQ(slewOcclusionFraction(0.3f, 1.0f, 0.0f), 0.3f);   // hold
}

// Repeated slewing converges monotonically to the target and never overshoots.
TEST(AudioOcclusionSystem, SlewConvergesWithoutOvershoot)
{
    float f = 0.0f;
    float prev = -1.0f;
    for (int i = 0; i < 200; ++i)
    {
        f = slewOcclusionFraction(f, 1.0f, 0.2f);
        EXPECT_GE(f, prev);       // monotonically rising
        EXPECT_LE(f, 1.0f);       // never past the target
        prev = f;
    }
    EXPECT_GT(f, 0.99f);          // and it actually arrives
}
