// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_physics_layer_filter.cpp
/// @brief Phase 10.9 Slice 7 Ph5 — pin the object-layer pair-filter contract.
///
/// Pre-Ph5 there was a single `CHARACTER` layer and the rule
/// "character vs character — never" prevented the player from
/// colliding with any NPC body assigned to that layer (ragdolls
/// included, if they were promoted to CHARACTER for character-class
/// behaviour). Ph5 splits the layer into PLAYER_CHARACTER and
/// NPC_CHARACTER; the filter now allows PLAYER vs NPC to collide
/// while keeping same-type pairs non-colliding (PLAYER vs PLAYER is
/// meaningless; NPC vs NPC is a deliberate gameplay choice — ragdoll
/// crowds shouldn't push each other).

#include <gtest/gtest.h>
#include "physics/physics_layers.h"

namespace Vestige::PhysicsLayerFilter::Test
{

// ShouldCollide is symmetric in Jolt's pair filter contract — the
// helper checks both (a,b) and (b,a) so the test name describes the
// rule rather than the argument order.
namespace
{
bool collides(JPH::ObjectLayer a, JPH::ObjectLayer b)
{
    ObjectLayerPairFilter filter;
    bool ab = filter.ShouldCollide(a, b);
    bool ba = filter.ShouldCollide(b, a);
    EXPECT_EQ(ab, ba) << "ShouldCollide must be symmetric";
    return ab;
}
}  // namespace

// PLAYER vs NPC — must collide so the player can shoulder-bump NPCs and
// NPC ragdolls react to the player walking into them. This is the
// headline Ph5 contract.
TEST(PhysicsLayerFilter, PlayerCharacterCollidesWithNpcCharacter_Ph5)
{
    EXPECT_TRUE(collides(ObjectLayers::PLAYER_CHARACTER,
                         ObjectLayers::NPC_CHARACTER));
}

// PLAYER vs PLAYER — meaningless (only one player exists), keep
// non-colliding so a body inadvertently set to PLAYER_CHARACTER
// doesn't cause a player-self-collision.
TEST(PhysicsLayerFilter, PlayerVsPlayerNeverCollides_Ph5)
{
    EXPECT_FALSE(collides(ObjectLayers::PLAYER_CHARACTER,
                          ObjectLayers::PLAYER_CHARACTER));
}

// NPC vs NPC — deliberate non-collision: an NPC crowd or ragdoll pile
// shouldn't push each other, since that produces character-on-character
// jitter that physics-based ragdoll simulation is famously bad at.
// Game projects that want NPCs to push each other can move them to
// DYNAMIC.
TEST(PhysicsLayerFilter, NpcVsNpcNeverCollides_Ph5)
{
    EXPECT_FALSE(collides(ObjectLayers::NPC_CHARACTER,
                          ObjectLayers::NPC_CHARACTER));
}

// Both character flavours must still collide with DYNAMIC and STATIC
// (the rest of the world). This pins the core invariant that the
// split didn't accidentally break floor / wall / object collision.
TEST(PhysicsLayerFilter, CharacterCollidesWithDynamicAndStatic)
{
    EXPECT_TRUE(collides(ObjectLayers::PLAYER_CHARACTER, ObjectLayers::DYNAMIC));
    EXPECT_TRUE(collides(ObjectLayers::PLAYER_CHARACTER, ObjectLayers::STATIC));
    EXPECT_TRUE(collides(ObjectLayers::NPC_CHARACTER,    ObjectLayers::DYNAMIC));
    EXPECT_TRUE(collides(ObjectLayers::NPC_CHARACTER,    ObjectLayers::STATIC));
}

// Pre-Ph5 the layer was simply `CHARACTER`. The header keeps that
// constant as an alias for `PLAYER_CHARACTER` so existing call sites
// (the character controller, scripted player rigs, third-party game
// code copy-pasting from older docs) keep working.
TEST(PhysicsLayerFilter, BackwardCompatCharacterAliasesPlayer)
{
    EXPECT_EQ(static_cast<int>(ObjectLayers::CHARACTER),
              static_cast<int>(ObjectLayers::PLAYER_CHARACTER));
}

// Trigger / static / dynamic invariants: regression-pin the existing
// rules so the Ph5 refactor didn't move them. Static-vs-static, trigger
// vs static, trigger vs trigger all stay non-colliding.
TEST(PhysicsLayerFilter, StaticVsStaticNeverCollides)
{
    EXPECT_FALSE(collides(ObjectLayers::STATIC, ObjectLayers::STATIC));
}
TEST(PhysicsLayerFilter, TriggerVsStaticNeverCollides)
{
    EXPECT_FALSE(collides(ObjectLayers::TRIGGER, ObjectLayers::STATIC));
}
TEST(PhysicsLayerFilter, TriggerVsTriggerNeverCollides)
{
    EXPECT_FALSE(collides(ObjectLayers::TRIGGER, ObjectLayers::TRIGGER));
}
TEST(PhysicsLayerFilter, DynamicVsDynamicCollides)
{
    EXPECT_TRUE(collides(ObjectLayers::DYNAMIC, ObjectLayers::DYNAMIC));
}

// Broadphase mapping: both character flavours map to the same broadphase
// layer so the broadphase acceleration structure isn't accidentally
// fragmented by the split.
TEST(PhysicsLayerFilter, BothCharacterLayersMapToSameBroadphase)
{
    BroadPhaseLayerMapping mapping;
    EXPECT_EQ(static_cast<JPH::BroadPhaseLayer::Type>(
                  mapping.GetBroadPhaseLayer(ObjectLayers::PLAYER_CHARACTER)),
              static_cast<JPH::BroadPhaseLayer::Type>(
                  mapping.GetBroadPhaseLayer(ObjectLayers::NPC_CHARACTER)));
}

}  // namespace Vestige::PhysicsLayerFilter::Test
