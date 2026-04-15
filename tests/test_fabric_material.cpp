// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_fabric_material.cpp
/// @brief Unit tests for the fabric material database and physics foundation.
#include <gtest/gtest.h>

#include "physics/fabric_material.h"
#include "physics/cloth_simulator.h"
#include "physics/rigid_body.h"
#include "physics/physics_world.h"
#include "scene/entity.h"

#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace Vestige;

// ===========================================================================
// Fabric Material Database
// ===========================================================================

TEST(FabricMaterial, AllTypesExist)
{
    int count = FabricDatabase::getCount();
    EXPECT_EQ(count, static_cast<int>(FabricType::COUNT));

    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        EXPECT_NE(mat.name, nullptr) << "Fabric index " << i << " has null name";
        EXPECT_GT(std::strlen(mat.name), 0u) << "Fabric index " << i << " has empty name";
    }
}

TEST(FabricMaterial, AllNamesUnique)
{
    std::set<std::string> names;
    int count = FabricDatabase::getCount();

    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        auto [it, inserted] = names.insert(mat.name);
        EXPECT_TRUE(inserted) << "Duplicate fabric name: " << mat.name;
    }
}

TEST(FabricMaterial, AllHavePositiveDensity)
{
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        EXPECT_GT(mat.densityGSM, 0.0f) << mat.name << " has non-positive density";
    }
}

TEST(FabricMaterial, AllHaveNonNegativeCompliance)
{
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        EXPECT_GE(mat.stretchCompliance, 0.0f) << mat.name;
        EXPECT_GE(mat.shearCompliance, 0.0f) << mat.name;
        EXPECT_GE(mat.bendCompliance, 0.0f) << mat.name;
    }
}

TEST(FabricMaterial, AllHaveValidDamping)
{
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        EXPECT_GE(mat.damping, 0.0f) << mat.name;
        EXPECT_LE(mat.damping, 1.0f) << mat.name;
    }
}

TEST(FabricMaterial, AllHaveValidFriction)
{
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        EXPECT_GE(mat.friction, 0.0f) << mat.name;
        EXPECT_LE(mat.friction, 1.0f) << mat.name;
    }
}

TEST(FabricMaterial, AllHavePositiveDragCoefficient)
{
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        EXPECT_GT(mat.dragCoefficient, 0.0f) << mat.name;
    }
}

TEST(FabricMaterial, AllHaveDescriptions)
{
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        EXPECT_NE(mat.description, nullptr) << mat.name;
        EXPECT_GT(std::strlen(mat.description), 0u) << mat.name;
    }
}

TEST(FabricMaterial, DensityOrderedLightToHeavy)
{
    // Common fabrics should be roughly ordered light → heavy
    EXPECT_LT(FabricDatabase::get(FabricType::CHIFFON).densityGSM,
              FabricDatabase::get(FabricType::LEATHER).densityGSM);
    EXPECT_LT(FabricDatabase::get(FabricType::SILK).densityGSM,
              FabricDatabase::get(FabricType::DENIM).densityGSM);
    EXPECT_LT(FabricDatabase::get(FabricType::COTTON).densityGSM,
              FabricDatabase::get(FabricType::CANVAS).densityGSM);
}

TEST(FabricMaterial, BiblicalFabricClassification)
{
    EXPECT_FALSE(FabricDatabase::isBiblical(FabricType::COTTON));
    EXPECT_FALSE(FabricDatabase::isBiblical(FabricType::SILK));
    EXPECT_FALSE(FabricDatabase::isBiblical(FabricType::LEATHER));

    EXPECT_TRUE(FabricDatabase::isBiblical(FabricType::FINE_LINEN));
    EXPECT_TRUE(FabricDatabase::isBiblical(FabricType::EMBROIDERED_VEIL));
    EXPECT_TRUE(FabricDatabase::isBiblical(FabricType::GOAT_HAIR));
    EXPECT_TRUE(FabricDatabase::isBiblical(FabricType::RAM_SKIN));
    EXPECT_TRUE(FabricDatabase::isBiblical(FabricType::TACHASH));
}

TEST(FabricMaterial, GoatHairIsHeaviest)
{
    // Historical fact: goat hair at 1588 GSM is the heaviest fabric
    float maxGSM = 0.0f;
    FabricType heaviest = FabricType::CHIFFON;
    int count = FabricDatabase::getCount();

    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        if (mat.densityGSM > maxGSM)
        {
            maxGSM = mat.densityGSM;
            heaviest = type;
        }
    }
    EXPECT_EQ(heaviest, FabricType::GOAT_HAIR);
}

TEST(FabricMaterial, GetNameMatchesMaterial)
{
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        const auto& mat = FabricDatabase::get(type);
        EXPECT_STREQ(FabricDatabase::getName(type), mat.name);
    }
}

TEST(FabricMaterial, OutOfRangeFallbacksToCotton)
{
    auto invalid = static_cast<FabricType>(255);
    const auto& mat = FabricDatabase::get(invalid);
    EXPECT_STREQ(mat.name, "Cotton");
}

// ===========================================================================
// Fabric → ClothPresetConfig conversion
// ===========================================================================

TEST(FabricPresetConversion, AllTypesConvert)
{
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        auto preset = FabricDatabase::toPresetConfig(type);

        EXPECT_GT(preset.solver.particleMass, 0.0f)
            << FabricDatabase::getName(type);
        EXPECT_GE(preset.solver.substeps, 1)
            << FabricDatabase::getName(type);
        EXPECT_GE(preset.windStrength, 0.0f)
            << FabricDatabase::getName(type);
        EXPECT_GT(preset.dragCoefficient, 0.0f)
            << FabricDatabase::getName(type);
    }
}

TEST(FabricPresetConversion, HeavierFabricsHaveMoreMass)
{
    auto chiffon = FabricDatabase::toPresetConfig(FabricType::CHIFFON);
    auto leather = FabricDatabase::toPresetConfig(FabricType::LEATHER);
    EXPECT_LT(chiffon.solver.particleMass, leather.solver.particleMass);
}

TEST(FabricPresetConversion, SofterFabricsHaveMoreSubsteps)
{
    auto chiffon = FabricDatabase::toPresetConfig(FabricType::CHIFFON);
    auto leather = FabricDatabase::toPresetConfig(FabricType::LEATHER);
    EXPECT_GE(chiffon.solver.substeps, leather.solver.substeps);
}

TEST(FabricPresetConversion, LighterFabricsHaveMoreWind)
{
    auto chiffon = FabricDatabase::toPresetConfig(FabricType::CHIFFON);
    auto leather = FabricDatabase::toPresetConfig(FabricType::LEATHER);
    EXPECT_GT(chiffon.windStrength, leather.windStrength);
}

TEST(FabricPresetConversion, FineLInenMatchesExistingPreset)
{
    // Fine Linen should produce values very close to the existing linenCurtain preset
    auto fabricPreset = FabricDatabase::toPresetConfig(FabricType::FINE_LINEN);
    auto linenPreset = ClothPresets::linenCurtain();

    EXPECT_NEAR(fabricPreset.solver.particleMass, linenPreset.solver.particleMass, 0.01f);
    EXPECT_NEAR(fabricPreset.solver.stretchCompliance,
                linenPreset.solver.stretchCompliance, 0.0001f);
    EXPECT_NEAR(fabricPreset.solver.bendCompliance,
                linenPreset.solver.bendCompliance, 0.01f);
}

TEST(FabricPresetConversion, CanInitializeSimulator)
{
    // Every fabric type should produce a valid ClothConfig that initializes
    int count = FabricDatabase::getCount();
    for (int i = 0; i < count; ++i)
    {
        auto type = static_cast<FabricType>(i);
        auto preset = FabricDatabase::toPresetConfig(type);
        preset.solver.width = 5;
        preset.solver.height = 5;

        ClothSimulator sim;
        sim.initialize(preset.solver);
        EXPECT_TRUE(sim.isInitialized())
            << FabricDatabase::getName(type) << " failed to initialize simulator";
        EXPECT_EQ(sim.getParticleCount(), 25u);
    }
}

// ===========================================================================
// Convex Hull collision shape
// ===========================================================================

TEST(CollisionShapes, ConvexHullCreation)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("HullBox");
    entity.transform.position = glm::vec3(0, 5, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::DYNAMIC;
    rb->shapeType = CollisionShapeType::CONVEX_HULL;
    rb->mass = 1.0f;

    // Provide a simple cube as 8 vertices
    rb->collisionVertices = {
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
    };

    rb->createBody(world);
    EXPECT_TRUE(rb->hasBody());

    world.shutdown();
}

TEST(CollisionShapes, ConvexHullTetrahedron)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("Tetrahedron");
    entity.transform.position = glm::vec3(0, 10, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::DYNAMIC;
    rb->shapeType = CollisionShapeType::CONVEX_HULL;

    // Minimal valid hull: 4 non-coplanar points
    rb->collisionVertices = {
        {0, 0, 0}, {1, 0, 0}, {0.5f, 1, 0}, {0.5f, 0.5f, 1},
    };

    rb->createBody(world);
    EXPECT_TRUE(rb->hasBody());

    world.shutdown();
}

TEST(CollisionShapes, ConvexHullTooFewVerticesFails)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("TooFew");
    entity.transform.position = glm::vec3(0, 0, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->shapeType = CollisionShapeType::CONVEX_HULL;

    // Only 3 points — not enough for a 3D convex hull
    rb->collisionVertices = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0},
    };

    rb->createBody(world);
    EXPECT_FALSE(rb->hasBody());

    world.shutdown();
}

TEST(CollisionShapes, ConvexHullWithManyVertices)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("Sphere");
    entity.transform.position = glm::vec3(0, 5, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::STATIC;
    rb->shapeType = CollisionShapeType::CONVEX_HULL;

    // Generate sphere approximation with many vertices
    for (int lat = 0; lat <= 20; ++lat)
    {
        float theta = static_cast<float>(lat) / 20.0f * 3.14159265f;
        for (int lon = 0; lon <= 20; ++lon)
        {
            float phi = static_cast<float>(lon) / 20.0f * 2.0f * 3.14159265f;
            float x = std::sin(theta) * std::cos(phi);
            float y = std::cos(theta);
            float z = std::sin(theta) * std::sin(phi);
            rb->collisionVertices.push_back(glm::vec3(x, y, z));
        }
    }

    rb->createBody(world);
    EXPECT_TRUE(rb->hasBody());

    world.shutdown();
}

// ===========================================================================
// Triangle mesh collision shape
// ===========================================================================

TEST(CollisionShapes, MeshShapeCreation)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("MeshFloor");
    entity.transform.position = glm::vec3(0, 0, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::STATIC;
    rb->shapeType = CollisionShapeType::MESH;

    // Simple quad as 2 triangles
    rb->collisionVertices = {
        {-5, 0, -5}, {5, 0, -5}, {5, 0, 5}, {-5, 0, 5},
    };
    rb->collisionIndices = {0, 1, 2, 0, 2, 3};

    rb->createBody(world);
    EXPECT_TRUE(rb->hasBody());

    world.shutdown();
}

TEST(CollisionShapes, MeshShapeForcesStatic)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("MeshDynamic");
    entity.transform.position = glm::vec3(0, 5, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->motionType = BodyMotionType::DYNAMIC;  // Will be forced to STATIC
    rb->shapeType = CollisionShapeType::MESH;

    rb->collisionVertices = {
        {-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1},
    };
    rb->collisionIndices = {0, 1, 2, 0, 2, 3};

    rb->createBody(world);
    EXPECT_TRUE(rb->hasBody());
    EXPECT_EQ(rb->motionType, BodyMotionType::STATIC);

    world.shutdown();
}

TEST(CollisionShapes, MeshShapeEmptyFails)
{
    PhysicsWorld world;
    ASSERT_TRUE(world.initialize());

    Entity entity("EmptyMesh");
    entity.transform.position = glm::vec3(0, 0, 0);
    entity.update(0.0f);

    auto* rb = entity.addComponent<RigidBody>();
    rb->shapeType = CollisionShapeType::MESH;
    // No vertices or indices

    rb->createBody(world);
    EXPECT_FALSE(rb->hasBody());

    world.shutdown();
}

// ===========================================================================
// setCollisionMesh utility
// ===========================================================================

TEST(CollisionShapes, SetCollisionMesh)
{
    RigidBody rb;

    std::vector<glm::vec3> positions = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3, 0, 1, 3, 1, 2, 3};

    rb.setCollisionMesh(positions.data(), positions.size(),
                        indices.data(), indices.size());

    EXPECT_EQ(rb.collisionVertices.size(), 4u);
    EXPECT_EQ(rb.collisionIndices.size(), 12u);
    EXPECT_NEAR(rb.collisionVertices[1].x, 1.0f, 1e-5f);
}

TEST(CollisionShapes, SetCollisionMeshVerticesOnly)
{
    RigidBody rb;

    std::vector<glm::vec3> positions = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1},
    };

    rb.setCollisionMesh(positions.data(), positions.size());

    EXPECT_EQ(rb.collisionVertices.size(), 8u);
    EXPECT_TRUE(rb.collisionIndices.empty());
}

// ===========================================================================
// Clone preserves collision mesh data
// ===========================================================================

TEST(CollisionShapes, ClonePreservesCollisionData)
{
    RigidBody rb;
    rb.shapeType = CollisionShapeType::CONVEX_HULL;
    rb.collisionVertices = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
    };
    rb.collisionIndices = {0, 1, 2, 0, 2, 3};

    auto cloned = rb.clone();
    auto* rbClone = static_cast<RigidBody*>(cloned.get());

    EXPECT_EQ(rbClone->shapeType, CollisionShapeType::CONVEX_HULL);
    EXPECT_EQ(rbClone->collisionVertices.size(), 4u);
    EXPECT_EQ(rbClone->collisionIndices.size(), 6u);
}
