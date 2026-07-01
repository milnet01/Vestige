// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_surface_material.cpp
/// @brief Unit tests for the physics surface-material tag (AX4 S2):
///        user-data pack/unpack, PhysicsWorld set->get round-trip, and the
///        RigidBody::createBody body-tagging bridge.
#include "physics/surface_material.h"
#include "physics/physics_world.h"
#include "physics/rigid_body.h"
#include "scene/entity.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <gtest/gtest.h>

#include <cstdint>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Pure pack / unpack helpers (no Jolt required)
// ---------------------------------------------------------------------------

TEST(SurfaceMaterial, PackUnpackRoundTrip)
{
    const EntityId entity = 0x0123ABCDu;
    const SurfaceMaterial mat = SurfaceMaterial::Metal;

    const std::uint64_t packed = packBodyTags(entity, mat);
    EXPECT_EQ(unpackEntity(packed), entity);
    EXPECT_EQ(unpackMaterial(packed), mat);
}

TEST(SurfaceMaterial, PackReservedBitsAreZero)
{
    // Layout: [63..40 reserved(0) | 39..8 entityId | 7..0 material].
    // Max entity id + max material must not bleed into the reserved bits.
    const std::uint64_t packed =
        packBodyTags(0xFFFFFFFFu, SurfaceMaterial::Glass);
    EXPECT_EQ(packed >> 40, 0u) << "reserved high bits must stay zero";
    EXPECT_EQ(unpackEntity(packed), 0xFFFFFFFFu);
    EXPECT_EQ(unpackMaterial(packed), SurfaceMaterial::Glass);
}

TEST(SurfaceMaterial, ZeroWordIsDefaultAndNoEntity)
{
    // An untagged body's user-data is 0 -> Default material, entity 0 (none).
    EXPECT_EQ(unpackMaterial(0ull), SurfaceMaterial::Default);
    EXPECT_EQ(unpackEntity(0ull), 0u);
}

TEST(SurfaceMaterial, EveryMemberRoundTrips)
{
    for (int i = 0; i <= static_cast<int>(SurfaceMaterial::Glass); ++i)
    {
        const auto mat = static_cast<SurfaceMaterial>(i);
        const std::uint64_t packed = packBodyTags(42u, mat);
        EXPECT_EQ(unpackMaterial(packed), mat);
        EXPECT_EQ(unpackEntity(packed), 42u);
    }
}

TEST(SurfaceMaterial, LabelsAreStable)
{
    EXPECT_STREQ(surfaceMaterialLabel(SurfaceMaterial::Default), "Default");
    EXPECT_STREQ(surfaceMaterialLabel(SurfaceMaterial::Stone), "Stone");
    EXPECT_STREQ(surfaceMaterialLabel(SurfaceMaterial::Glass), "Glass");
    EXPECT_STREQ(surfaceMaterialLabel(SurfaceMaterial::Grass), "Grass");
}

// AX4 S9 — the editor material-assign picker iterates [0, kSurfaceMaterialCount)
// and labels each entry via surfaceMaterialLabel. Guard that the count exactly
// spans the enum: it must reach Glass (the last member) and every index in
// range must carry its own non-"Default" label (a stale count would drop the
// tail materials from the dropdown or mislabel them).
TEST(SurfaceMaterial, CountSpansTheEnum)
{
    EXPECT_EQ(kSurfaceMaterialCount,
              static_cast<int>(SurfaceMaterial::Glass) + 1);
    for (int i = 1; i < kSurfaceMaterialCount; ++i)
    {
        EXPECT_STRNE(surfaceMaterialLabel(static_cast<SurfaceMaterial>(i)),
                     "Default")
            << "index " << i << " has no distinct label";
    }
}

// ---------------------------------------------------------------------------
// PhysicsWorld set -> get round-trip (main-thread accessors)
// ---------------------------------------------------------------------------

TEST(SurfaceMaterial, PhysicsWorldSetGetRoundTrip)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    JPH::BoxShape* shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
    JPH::BodyID body = world.createStaticBody(shape, glm::vec3(0.0f));
    ASSERT_FALSE(body.IsInvalid());

    // Untagged default: a freshly created body has zeroed user-data.
    EXPECT_EQ(world.getSurfaceMaterial(body), SurfaceMaterial::Default);
    EXPECT_EQ(world.getBodyEntity(body), 0u);

    world.setBodyTags(body, 7u, SurfaceMaterial::Wood);
    EXPECT_EQ(world.getSurfaceMaterial(body), SurfaceMaterial::Wood);
    EXPECT_EQ(world.getBodyEntity(body), 7u);

    world.shutdown();
}

TEST(SurfaceMaterial, PhysicsWorldInvalidBodyIsDefault)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    EXPECT_EQ(world.getSurfaceMaterial(JPH::BodyID()), SurfaceMaterial::Default);
    EXPECT_EQ(world.getBodyEntity(JPH::BodyID()), 0u);
    // Tagging an invalid body is a no-op (must not crash).
    world.setBodyTags(JPH::BodyID(), 1u, SurfaceMaterial::Stone);

    world.shutdown();
}

// ---------------------------------------------------------------------------
// RigidBody::createBody tags the body with its owning entity + material
// ---------------------------------------------------------------------------

TEST(SurfaceMaterial, RigidBodyTagsBodyOnCreate)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("StoneFloor");
    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::STATIC;
    rb->shapeType = CollisionShapeType::BOX;
    rb->shapeSize = glm::vec3(0.5f);
    rb->surfaceMaterial = SurfaceMaterial::Stone;

    entity.update(0.0f);  // compute the world matrix
    rb->createBody(world);
    ASSERT_TRUE(rb->hasBody());

    EXPECT_EQ(world.getSurfaceMaterial(rb->getBodyId()), SurfaceMaterial::Stone);
    EXPECT_EQ(world.getBodyEntity(rb->getBodyId()), entity.getId());

    world.shutdown();
}

TEST(SurfaceMaterial, RigidBodyDefaultMaterialTagged)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("Untagged");
    auto* rb = entity.addComponent<RigidBody>();
    rb->shapeSize = glm::vec3(0.5f);
    entity.update(0.0f);
    rb->createBody(world);
    ASSERT_TRUE(rb->hasBody());

    // No material assigned -> Default, but the entity id is still tagged.
    EXPECT_EQ(world.getSurfaceMaterial(rb->getBodyId()), SurfaceMaterial::Default);
    EXPECT_EQ(world.getBodyEntity(rb->getBodyId()), entity.getId());

    world.shutdown();
}

TEST(SurfaceMaterial, RigidBodyCloneCopiesMaterial)
{
    RigidBody rb;
    rb.surfaceMaterial = SurfaceMaterial::Sand;

    auto copy = rb.clone();
    auto* copyRb = dynamic_cast<RigidBody*>(copy.get());
    ASSERT_NE(copyRb, nullptr);
    EXPECT_EQ(copyRb->surfaceMaterial, SurfaceMaterial::Sand);
}
