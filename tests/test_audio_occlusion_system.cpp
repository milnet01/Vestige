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
#include "core/job_system.h"
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

// ---------------------------------------------------------------------------
// S4 — MT2 parallel batch + budget planner
// ---------------------------------------------------------------------------

// The batched parallel path in a synchronous JobSystem must produce bit-
// identical results to calling the serial measureOcclusion per source — the
// determinism guarantee tests depend on (and replay needs).
TEST(AudioOcclusionSystem, BatchInSyncModeEqualsSerial)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    // A blocking wall and a clear lane, so the batch mixes blocked + open.
    makeWall(world, glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(4.0f, 4.0f, 0.5f),
             SurfaceMaterial::Metal);

    const glm::vec3 listener(0.0f);
    std::vector<OcclusionQuery> queries;
    queries.push_back({listener, glm::vec3(0.0f, 0.0f, 10.0f), 8, 0.5f, {}});
    queries.push_back({listener, glm::vec3(20.0f, 0.0f, 0.0f), 8, 0.5f, {}}); // clear
    queries.push_back({listener, glm::vec3(0.0f, 0.0f, 10.0f), 1, 0.0f, {}}); // binary

    JobSystem jobs(JobSystemConfig{0});  // synchronous — deterministic
    ASSERT_TRUE(jobs.isSynchronous());
    const std::vector<OcclusionMeasurement> batch =
        measureOcclusionBatch(world, jobs, queries);

    ASSERT_EQ(batch.size(), queries.size());
    for (std::size_t i = 0; i < queries.size(); ++i)
    {
        const OcclusionMeasurement serial = measureOcclusion(
            world, queries[i].listenerPos, queries[i].sourcePos,
            queries[i].rayCount, queries[i].sourceRadius, queries[i].ignoreBody);
        EXPECT_EQ(batch[i].blocked, serial.blocked) << "query " << i;
        EXPECT_FLOAT_EQ(batch[i].targetFraction, serial.targetFraction)
            << "query " << i;
        EXPECT_EQ(batch[i].material, serial.material) << "query " << i;
    }

    world.shutdown();
}

// An empty batch is a valid no-op (empty scene / all culled): parallelFor(0)
// completes, wait no-ops, and no rays are cast.
TEST(AudioOcclusionSystem, EmptyBatchIsNoOp)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());
    JobSystem jobs(JobSystemConfig{0});
    EXPECT_TRUE(measureOcclusionBatch(world, jobs, {}).empty());
    world.shutdown();
}

// Under budget, every eligible source is serviced and the cursor resets — the
// default-settings case (256 rays < 512 budget), so round-robin never engages.
TEST(AudioOcclusionSystem, BudgetPlanServicesAllWhenUnderBudget)
{
    const OcclusionServicingPlan plan =
        planOcclusionServicing(/*eligible*/ 32, /*rayCount*/ 8,
                               /*budget*/ 512, /*cursor*/ 0);
    EXPECT_FALSE(plan.engaged);
    EXPECT_EQ(plan.serviced.size(), 32u);
    EXPECT_EQ(plan.nextCursor, 0);
}

// Over budget, only the largest prefix that fits is serviced, starting at the
// cursor and wrapping, with the cursor advancing so later frames cover the rest.
TEST(AudioOcclusionSystem, BudgetPlanRoundRobinsWhenOverBudget)
{
    // 10 sources × 8 rays = 80 offered, budget 40 → 5 serviceable per frame.
    const OcclusionServicingPlan f0 =
        planOcclusionServicing(10, 8, 40, 0);
    EXPECT_TRUE(f0.engaged);
    ASSERT_EQ(f0.serviced.size(), 5u);
    EXPECT_EQ(f0.serviced.front(), 0);
    EXPECT_EQ(f0.serviced.back(), 4);
    EXPECT_EQ(f0.nextCursor, 5);

    // Next frame resumes at the cursor and wraps around the end.
    const OcclusionServicingPlan f1 =
        planOcclusionServicing(10, 8, 40, f0.nextCursor);
    EXPECT_TRUE(f1.engaged);
    ASSERT_EQ(f1.serviced.size(), 5u);
    EXPECT_EQ(f1.serviced.front(), 5);
    EXPECT_EQ(f1.serviced.back(), 9);
    EXPECT_EQ(f1.nextCursor, 0);  // (5 + 5) % 10
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
