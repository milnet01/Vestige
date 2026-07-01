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
    EXPECT_FALSE(
        measureOcclusion(world, listener, source, 1, 0.0f, ownBody).blocked);

    world.shutdown();
}

// ---------------------------------------------------------------------------
// S3 — multi-ray volumetric fraction + nearest-blocker material
// ---------------------------------------------------------------------------

// A wall large enough to cover the whole source sphere blocks every ray →
// fraction 1 (matches the single-ray fully-behind-wall result).
TEST(AudioOcclusionSystem, MultiRayFullyBehindWallIsFullyOccluded)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    const glm::vec3 listener(0.0f);
    const glm::vec3 source(0.0f, 0.0f, 10.0f);
    makeWall(world, glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(4.0f, 4.0f, 0.5f),
             SurfaceMaterial::Stone);

    const OcclusionMeasurement m =
        measureOcclusion(world, listener, source, 8, 0.5f);
    EXPECT_TRUE(m.blocked);
    EXPECT_FLOAT_EQ(m.targetFraction, 1.0f);

    world.shutdown();
}

// A wall covering only part of the source sphere (its top half) blocks some
// rays but not all → an intermediate fraction, strictly between 0 and 1. This
// is the graded open-path behaviour near an edge that a single ray can't give.
TEST(AudioOcclusionSystem, MultiRayPartialCoverGivesIntermediateFraction)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    const glm::vec3 listener(0.0f);
    const glm::vec3 source(0.0f, 0.0f, 10.0f);
    // A slab occluding only y > 0 near the source: centred above the line so
    // its lower face sits at y = 0. Rays toward the upper hemisphere of the
    // 0.5 m source sphere are blocked; lower-hemisphere rays pass.
    makeWall(world, glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(4.0f, 2.0f, 0.5f),
             SurfaceMaterial::Wood);

    const OcclusionMeasurement m =
        measureOcclusion(world, listener, source, 16, 0.5f);
    EXPECT_TRUE(m.blocked);
    EXPECT_GT(m.targetFraction, 0.0f);
    EXPECT_LT(m.targetFraction, 1.0f);

    world.shutdown();
}

// When two walls both block, the material comes from the NEAREST one (smallest
// hit distance across every blocked ray), not whichever ray happened to be
// last.
TEST(AudioOcclusionSystem, MultiRayPicksNearestBlockerMaterial)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    const glm::vec3 listener(0.0f);
    const glm::vec3 source(0.0f, 0.0f, 10.0f);
    // Near wall (Metal) at z = 3, far wall (Stone) at z = 7. Both fully cover
    // the source sphere, so every ray hits the near wall first.
    makeWall(world, glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(4.0f, 4.0f, 0.5f),
             SurfaceMaterial::Metal);
    makeWall(world, glm::vec3(0.0f, 0.0f, 7.0f), glm::vec3(4.0f, 4.0f, 0.5f),
             SurfaceMaterial::Stone);

    const OcclusionMeasurement m =
        measureOcclusion(world, listener, source, 8, 0.5f);
    EXPECT_TRUE(m.blocked);
    EXPECT_EQ(m.material, occlusionPresetForSurface(SurfaceMaterial::Metal));

    world.shutdown();
}

// The offset table is the deterministic point set the fan samples: fixed size,
// ray 0 at the exact centre, and every other entry a unit vector (so scaling
// by the radius lands it on the source sphere). Stable across calls — no RNG.
TEST(AudioOcclusionSystem, RayOffsetTableIsStableAndUnit)
{
    const auto& a = occlusionRayOffsets();
    const auto& b = occlusionRayOffsets();
    ASSERT_EQ(a.size(), static_cast<std::size_t>(kMaxOcclusionRayCount));

    EXPECT_FLOAT_EQ(glm::length(a[0]), 0.0f) << "ray 0 must be the centre";
    for (int i = 1; i < kMaxOcclusionRayCount; ++i)
    {
        EXPECT_NEAR(glm::length(a[i]), 1.0f, 1e-5f)
            << "offset " << i << " must be a unit vector";
        EXPECT_EQ(a[i], b[i]) << "offset " << i << " must be deterministic";
    }
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
