// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_impact_audio_system.cpp
/// @brief AX4 S7 — collision-impact emission policy. Exercises the pure,
///        engine-free decision pieces (material-hardness dominance, the
///        threshold / untagged-suppression gate) and the system's no-op guard.
#include "systems/impact_audio_system.h"
#include "physics/contact_event.h"

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{

/// Build an Enter CollisionEvent with the fields the impact policy reads.
CollisionEvent enterEvent(SurfaceMaterial a, SurfaceMaterial b, float approachSpeed)
{
    CollisionEvent e;
    e.isEnter = true;
    e.approachSpeed = approachSpeed;
    e.matA = a;
    e.matB = b;
    e.point = glm::vec3(1.0f, 2.0f, 3.0f);
    return e;
}

}  // namespace

// --- Material hardness ranking ---------------------------------------------

TEST(MaterialHardness, RanksHardestToSoftest)
{
    // Metal > Stone > Glass > Wood > Dirt > Grass > Sand > Cloth > Water (§8).
    EXPECT_GT(materialHardnessRank(SurfaceMaterial::Metal), materialHardnessRank(SurfaceMaterial::Stone));
    EXPECT_GT(materialHardnessRank(SurfaceMaterial::Stone), materialHardnessRank(SurfaceMaterial::Glass));
    EXPECT_GT(materialHardnessRank(SurfaceMaterial::Glass), materialHardnessRank(SurfaceMaterial::Wood));
    EXPECT_GT(materialHardnessRank(SurfaceMaterial::Wood),  materialHardnessRank(SurfaceMaterial::Dirt));
    EXPECT_GT(materialHardnessRank(SurfaceMaterial::Dirt),  materialHardnessRank(SurfaceMaterial::Grass));
    EXPECT_GT(materialHardnessRank(SurfaceMaterial::Grass), materialHardnessRank(SurfaceMaterial::Sand));
    EXPECT_GT(materialHardnessRank(SurfaceMaterial::Sand),  materialHardnessRank(SurfaceMaterial::Cloth));
    EXPECT_GT(materialHardnessRank(SurfaceMaterial::Cloth), materialHardnessRank(SurfaceMaterial::Water));
}

TEST(MaterialHardness, DefaultIsSoftestOfAll)
{
    // Untagged must rank below every real material so a single tagged body
    // always wins the timbre (design §8).
    EXPECT_LT(materialHardnessRank(SurfaceMaterial::Default), materialHardnessRank(SurfaceMaterial::Water));
}

// --- Harder-material pick ---------------------------------------------------

TEST(HarderMaterial, PicksTheHarderOfTwo)
{
    EXPECT_EQ(harderMaterial(SurfaceMaterial::Wood, SurfaceMaterial::Metal), SurfaceMaterial::Metal);
    EXPECT_EQ(harderMaterial(SurfaceMaterial::Metal, SurfaceMaterial::Wood), SurfaceMaterial::Metal);
    EXPECT_EQ(harderMaterial(SurfaceMaterial::Sand, SurfaceMaterial::Water), SurfaceMaterial::Sand);
}

TEST(HarderMaterial, TaggedBeatsDefault)
{
    // A Default (untagged) partner never dominates a real material.
    EXPECT_EQ(harderMaterial(SurfaceMaterial::Default, SurfaceMaterial::Wood), SurfaceMaterial::Wood);
    EXPECT_EQ(harderMaterial(SurfaceMaterial::Cloth, SurfaceMaterial::Default), SurfaceMaterial::Cloth);
}

// --- Impact decision policy -------------------------------------------------

TEST(DecideImpact, PlaysHarderMaterialOnAudibleEnter)
{
    const ImpactDecision d = decideImpact(enterEvent(SurfaceMaterial::Wood, SurfaceMaterial::Stone, 3.0f), false);
    EXPECT_TRUE(d.play);
    EXPECT_EQ(d.material, SurfaceMaterial::Stone);
}

TEST(DecideImpact, ExitEventIsSilent)
{
    CollisionEvent exit = enterEvent(SurfaceMaterial::Metal, SurfaceMaterial::Stone, 5.0f);
    exit.isEnter = false;   // separation carries no geometry / no sound
    EXPECT_FALSE(decideImpact(exit, false).play);
}

TEST(DecideImpact, SubThresholdIsSilent)
{
    // Below kMinImpactSpeed a feather-touch stays silent (but still raises a
    // scripting enter — that gate lives here, in the audio subscriber, §4/§8).
    const float gentle = kMinImpactSpeed * 0.5f;
    EXPECT_FALSE(decideImpact(enterEvent(SurfaceMaterial::Metal, SurfaceMaterial::Stone, gentle), false).play);
    // At/above the threshold it sounds.
    EXPECT_TRUE(decideImpact(enterEvent(SurfaceMaterial::Metal, SurfaceMaterial::Stone, kMinImpactSpeed), false).play);
}

TEST(DecideImpact, BothUntaggedSuppressedByDefault)
{
    // Untagged↔untagged is silent unless force-enabled, so an unauthored scene
    // full of Default boxes does not thud on every contact (§8).
    const CollisionEvent e = enterEvent(SurfaceMaterial::Default, SurfaceMaterial::Default, 4.0f);
    EXPECT_FALSE(decideImpact(e, /*emitUntaggedCollisions*/ false).play);
    EXPECT_TRUE(decideImpact(e, /*emitUntaggedCollisions*/ true).play);
}

TEST(DecideImpact, OneTaggedBodyStillSounds)
{
    // A Default-vs-tagged contact is audible (only both-untagged is suppressed);
    // the tagged side supplies the timbre.
    const ImpactDecision d = decideImpact(enterEvent(SurfaceMaterial::Default, SurfaceMaterial::Metal, 2.0f), false);
    EXPECT_TRUE(d.play);
    EXPECT_EQ(d.material, SurfaceMaterial::Metal);
}

// --- System safety ----------------------------------------------------------

TEST(ImpactAudioSystem, UpdateBeforeInitializeIsANoOp)
{
    // No bus/audio resolved yet — the event-driven system does no per-frame
    // work and must not touch its null dependencies.
    ImpactAudioSystem system;
    EXPECT_NO_FATAL_FAILURE(system.update(1.0f / 60.0f));
}
