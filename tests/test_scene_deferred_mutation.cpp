// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scene_deferred_mutation.cpp
/// @brief Red/green pins for Phase 10.9 Slice 3 S2 — the component /
///        entity mutation contract during an active `forEachEntity`
///        traversal. Resolves the scripting-vs-update collision
///        Phase 11B AI will trigger: an `OnUpdate` script node that
///        calls `SpawnEntity` / `DestroyEntity` runs inside the
///        per-frame walk, and today that mutates the walked hierarchy
///        under the iterator's feet (iterator invalidation on
///        `std::vector::erase`, rehash on `std::unordered_map::insert`).
///
/// The contract S2 introduces:
///   1. `Scene` exposes `isUpdating()` + a nesting-safe depth counter.
///   2. `forEachEntity` auto-wraps its traversal in `ScopedUpdate`.
///   3. `createEntity` / `removeEntity` called while `isUpdating()` is
///      true are queued, not applied immediately. Lookups by id still
///      resolve the created entity (so a spawn-then-write-output node
///      works) and the removed entity (so a script can see its own id
///      until the drain). Queued mutations apply when the outermost
///      `ScopedUpdate` releases.
///   4. No iterator invalidation during the traversal.

#include "scene/scene.h"
#include "scene/camera_component.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Update-depth plumbing — the foundation the queue sits on.
// ---------------------------------------------------------------------------

TEST(SceneDeferredMutation, IsUpdatingFalseByDefault_S2)
{
    Scene scene;
    EXPECT_FALSE(scene.isUpdating());
}

TEST(SceneDeferredMutation, ForEachEntityWrapsIsUpdatingTrue_S2)
{
    Scene scene;
    scene.createEntity("A");
    scene.createEntity("B");

    bool seenTrueInside = false;
    scene.forEachEntity([&](Entity&)
    {
        if (scene.isUpdating())
        {
            seenTrueInside = true;
        }
    });

    EXPECT_TRUE(seenTrueInside);
    EXPECT_FALSE(scene.isUpdating());
}

TEST(SceneDeferredMutation, ScopedUpdateIncrementsDepth_S2)
{
    Scene scene;
    EXPECT_FALSE(scene.isUpdating());
    {
        Scene::ScopedUpdate guard(scene);
        EXPECT_TRUE(scene.isUpdating());
        {
            Scene::ScopedUpdate inner(scene);
            EXPECT_TRUE(scene.isUpdating());
        }
        // Outer still active: nested release must NOT drop depth to zero.
        EXPECT_TRUE(scene.isUpdating());
    }
    EXPECT_FALSE(scene.isUpdating());
}

// ---------------------------------------------------------------------------
// Removal-during-traversal — the headline collision.
// ---------------------------------------------------------------------------

TEST(SceneDeferredMutation, RemoveSelfDuringForEachEntityVisitsAllSiblings_S2)
{
    Scene scene;
    Entity* a = scene.createEntity("A");
    Entity* b = scene.createEntity("B");
    Entity* c = scene.createEntity("C");

    const uint32_t bId = b->getId();

    std::vector<std::string> names;
    scene.forEachEntity([&](Entity& e)
    {
        names.push_back(e.getName());
        if (e.getName() == "B")
        {
            // Script-graph DestroyEntity shape: mutate the scene mid-walk.
            // Contract: the rest of the traversal must still run.
            scene.removeEntity(e.getId());
        }
    });

    // All three visited despite B's self-delete during iteration.
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "A");
    EXPECT_EQ(names[1], "B");
    EXPECT_EQ(names[2], "C");

    // Queued removal must take effect when the traversal ends.
    EXPECT_EQ(scene.findEntityById(bId), nullptr);
    EXPECT_NE(scene.findEntityById(a->getId()), nullptr);
    EXPECT_NE(scene.findEntityById(c->getId()), nullptr);
}

TEST(SceneDeferredMutation, RemoveSiblingDuringForEachEntityStillVisitsIt_S2)
{
    Scene scene;
    Entity* a = scene.createEntity("A");
    Entity* b = scene.createEntity("B");
    Entity* c = scene.createEntity("C");

    const uint32_t cId = c->getId();

    int visits = 0;
    scene.forEachEntity([&](Entity& e)
    {
        ++visits;
        if (e.getName() == "A")
        {
            // Queue an entity that hasn't been visited yet. The in-flight
            // traversal must still reach C — the removal takes effect
            // AFTER the visit pass, not during.
            scene.removeEntity(cId);
        }
    });

    EXPECT_EQ(visits, 3);
    EXPECT_EQ(scene.findEntityById(cId), nullptr);
    EXPECT_NE(scene.findEntityById(a->getId()), nullptr);
    EXPECT_NE(scene.findEntityById(b->getId()), nullptr);
}

TEST(SceneDeferredMutation, RemoveOutsideUpdateIsImmediate_S2)
{
    // The deferral only applies while isUpdating() is true; the
    // contract preserves the direct / editor mutation path.
    Scene scene;
    Entity* a = scene.createEntity("A");
    const uint32_t aId = a->getId();

    EXPECT_FALSE(scene.isUpdating());
    EXPECT_TRUE(scene.removeEntity(aId));
    EXPECT_EQ(scene.findEntityById(aId), nullptr);
}

// ---------------------------------------------------------------------------
// Creation-during-traversal — the spawn half.
// ---------------------------------------------------------------------------

TEST(SceneDeferredMutation, CreateDuringForEachEntityDoesNotCrash_S2)
{
    Scene scene;
    scene.createEntity("A");

    int visits = 0;
    bool spawned = false;
    scene.forEachEntity([&](Entity& e)
    {
        ++visits;
        if (!spawned && e.getName() == "A")
        {
            // SpawnEntity shape. Under current code this can invalidate
            // m_root->m_children if the vector reallocates on push_back.
            Entity* spawnedEnt = scene.createEntity("Spawned");
            ASSERT_NE(spawnedEnt, nullptr);
            // The spawn is live enough for the script to write back
            // its id (SpawnEntity's "entity" output pin).
            EXPECT_EQ(spawnedEnt->getName(), "Spawned");
            spawned = true;
        }
    });

    // Contract: the in-flight traversal must NOT see the new spawn
    // (otherwise a spawn-in-loop would infinite-loop; also it's a
    // cleaner frame-boundary semantic for scripting).
    EXPECT_EQ(visits, 1);
    EXPECT_TRUE(spawned);
}

TEST(SceneDeferredMutation, CreateDuringForEachEntityIsVisibleToFindById_S2)
{
    Scene scene;
    scene.createEntity("A");

    Entity* spawnedEnt = nullptr;
    scene.forEachEntity([&](Entity&)
    {
        if (!spawnedEnt)
        {
            spawnedEnt = scene.createEntity("Spawned");
        }
    });

    ASSERT_NE(spawnedEnt, nullptr);
    // After drain, the spawn IS attached; findEntityById still resolves.
    EXPECT_EQ(scene.findEntityById(spawnedEnt->getId()), spawnedEnt);
}

TEST(SceneDeferredMutation, CreateDuringForEachEntityIsWalkedNextPass_S2)
{
    Scene scene;
    scene.createEntity("A");

    // First pass spawns B.
    scene.forEachEntity([&](Entity& e)
    {
        if (e.getName() == "A")
        {
            scene.createEntity("B");
        }
    });

    // Second pass: both are visited.
    std::vector<std::string> names;
    scene.forEachEntity([&](Entity& e) { names.push_back(e.getName()); });

    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "A");
    EXPECT_EQ(names[1], "B");
}

// ---------------------------------------------------------------------------
// Interleaved spawn + destroy (the most adversarial shape).
// ---------------------------------------------------------------------------

TEST(SceneDeferredMutation, InterleavedSpawnAndDestroyDrainBothKinds_S2)
{
    Scene scene;
    Entity* a = scene.createEntity("A");
    Entity* b = scene.createEntity("B");

    Entity* spawned = nullptr;
    const uint32_t bId = b->getId();

    scene.forEachEntity([&](Entity& e)
    {
        if (e.getName() == "A")
        {
            spawned = scene.createEntity("Spawned");
            scene.removeEntity(bId);
        }
    });

    ASSERT_NE(spawned, nullptr);
    EXPECT_EQ(scene.findEntityById(bId), nullptr);
    EXPECT_NE(scene.findEntityById(a->getId()), nullptr);
    EXPECT_EQ(scene.findEntityById(spawned->getId()), spawned);
}

// ---------------------------------------------------------------------------
// Double-remove idempotency — a script that queues the same id twice
// (e.g. two OnEvent handlers both destroying the shared target) must
// not crash during drain.
// ---------------------------------------------------------------------------

TEST(SceneDeferredMutation, DoubleRemoveSameIdDuringUpdateIsIdempotent_S2)
{
    Scene scene;
    Entity* a = scene.createEntity("A");
    Entity* b = scene.createEntity("B");
    const uint32_t bId = b->getId();

    scene.forEachEntity([&](Entity& e)
    {
        if (e.getName() == "A" || e.getName() == "B")
        {
            scene.removeEntity(bId);
        }
    });

    EXPECT_EQ(scene.findEntityById(bId), nullptr);
    EXPECT_NE(scene.findEntityById(a->getId()), nullptr);
}

// ---------------------------------------------------------------------------
// Hierarchy: a script inside a child's update destroys the child.
// ---------------------------------------------------------------------------

TEST(SceneDeferredMutation, RemoveChildDuringDeepForEachSafe_S2)
{
    Scene scene;
    Entity* parent = scene.createEntity("Parent");
    auto child = std::make_unique<Entity>("Child");
    Entity* childPtr = parent->addChild(std::move(child));
    scene.registerEntityRecursive(childPtr);

    const uint32_t childId = childPtr->getId();
    int visits = 0;

    scene.forEachEntity([&](Entity& e)
    {
        ++visits;
        if (e.getName() == "Child")
        {
            scene.removeEntity(childId);
        }
    });

    // Both Parent and Child were reached; Child's removal applied on drain.
    EXPECT_EQ(visits, 2);
    EXPECT_EQ(scene.findEntityById(childId), nullptr);
    EXPECT_NE(scene.findEntityById(parent->getId()), nullptr);
}
