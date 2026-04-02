/// @file test_advanced_physics.cpp
/// @brief Unit tests for Batch 12: Ragdoll, Interaction, Fracture, Dismemberment.

#include <gtest/gtest.h>

#include "physics/ragdoll_preset.h"
#include "physics/ragdoll.h"
#include "physics/grab_system.h"
#include "physics/fracture.h"
#include "physics/deformable_mesh.h"
#include "physics/breakable_component.h"
#include "physics/dismemberment_zones.h"
#include "physics/dismemberment.h"
#include "scene/interactable_component.h"
#include "animation/skeleton.h"
#include "renderer/mesh.h"
#include "utils/aabb.h"

using namespace Vestige;

// ============================================================
// Helper: Create a simple test skeleton
// ============================================================

static std::shared_ptr<Skeleton> createTestSkeleton()
{
    auto skeleton = std::make_shared<Skeleton>();

    // Simple humanoid-like skeleton
    // 0: Hips (root)
    // 1: Spine
    // 2: Chest
    // 3: Head
    // 4: LeftUpperArm
    // 5: LeftLowerArm
    // 6: LeftHand
    // 7: RightUpperArm
    // 8: RightLowerArm
    // 9: RightHand
    // 10: LeftUpperLeg
    // 11: LeftLowerLeg
    // 12: LeftFoot
    // 13: RightUpperLeg
    // 14: RightLowerLeg
    // 15: RightFoot

    auto addJoint = [&](const std::string& name, int parent)
    {
        Joint joint;
        joint.name = name;
        joint.parentIndex = parent;
        joint.inverseBindMatrix = glm::mat4(1.0f);
        joint.localBindTransform = glm::mat4(1.0f);
        skeleton->m_joints.push_back(joint);
    };

    addJoint("Hips", -1);          // 0
    addJoint("Spine", 0);          // 1
    addJoint("Chest", 1);         // 2
    addJoint("Head", 2);          // 3
    addJoint("LeftUpperArm", 2);  // 4
    addJoint("LeftLowerArm", 4);  // 5
    addJoint("LeftHand", 5);      // 6
    addJoint("RightUpperArm", 2); // 7
    addJoint("RightLowerArm", 7); // 8
    addJoint("RightHand", 8);     // 9
    addJoint("LeftUpperLeg", 0);  // 10
    addJoint("LeftLowerLeg", 10); // 11
    addJoint("LeftFoot", 11);     // 12
    addJoint("RightUpperLeg", 0); // 13
    addJoint("RightLowerLeg", 13);// 14
    addJoint("RightFoot", 14);    // 15

    return skeleton;
}

/// Create identity bone matrices (16 joints)
static std::vector<glm::mat4> createIdentityBoneMatrices(int count = 16)
{
    std::vector<glm::mat4> matrices(static_cast<size_t>(count), glm::mat4(1.0f));

    // Give some spatial variation
    matrices[0] = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0));  // Hips at y=1
    matrices[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.2f, 0)); // Spine
    matrices[2] = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.4f, 0)); // Chest
    matrices[3] = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.7f, 0)); // Head
    matrices[4] = glm::translate(glm::mat4(1.0f), glm::vec3(-0.2f, 1.4f, 0)); // LUpperArm
    matrices[5] = glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 1.4f, 0)); // LLowerArm
    matrices[6] = glm::translate(glm::mat4(1.0f), glm::vec3(-0.8f, 1.4f, 0)); // LHand
    matrices[7] = glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, 1.4f, 0));  // RUpperArm
    matrices[8] = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 1.4f, 0));  // RLowerArm
    matrices[9] = glm::translate(glm::mat4(1.0f), glm::vec3(0.8f, 1.4f, 0));  // RHand
    matrices[10] = glm::translate(glm::mat4(1.0f), glm::vec3(-0.1f, 0.6f, 0));// LUpperLeg
    matrices[11] = glm::translate(glm::mat4(1.0f), glm::vec3(-0.1f, 0.3f, 0));// LLowerLeg
    matrices[12] = glm::translate(glm::mat4(1.0f), glm::vec3(-0.1f, 0.0f, 0));// LFoot
    matrices[13] = glm::translate(glm::mat4(1.0f), glm::vec3(0.1f, 0.6f, 0)); // RUpperLeg
    matrices[14] = glm::translate(glm::mat4(1.0f), glm::vec3(0.1f, 0.3f, 0)); // RLowerLeg
    matrices[15] = glm::translate(glm::mat4(1.0f), glm::vec3(0.1f, 0.0f, 0)); // RFoot

    return matrices;
}

/// Create a simple box mesh for fracture testing
static void createBoxVerticesAndIndices(
    std::vector<glm::vec3>& outPositions,
    std::vector<uint32_t>& outIndices)
{
    // Unit cube centered at origin
    outPositions = {
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
        {0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, {0.5f, -0.5f,  0.5f},
        {0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}
    };

    outIndices = {
        // Front
        0, 1, 2,  0, 2, 3,
        // Back
        5, 4, 7,  5, 7, 6,
        // Left
        4, 0, 3,  4, 3, 7,
        // Right
        1, 5, 6,  1, 6, 2,
        // Top
        3, 2, 6,  3, 6, 7,
        // Bottom
        4, 5, 1,  4, 1, 0
    };
}

/// Create skinned vertices with bone weights for dismemberment testing
static std::vector<Vertex> createSkinnedVertices()
{
    std::vector<Vertex> vertices;

    // Create vertices along a vertical line, weighted to different bones
    // Body vertices (weighted to Hips=0, Spine=1, Chest=2)
    for (int i = 0; i < 5; ++i)
    {
        Vertex v;
        v.position = glm::vec3(0.0f, static_cast<float>(i) * 0.3f, 0.0f);
        v.normal = glm::vec3(0, 0, 1);
        v.boneIds = glm::ivec4(0, 1, 0, 0);
        v.boneWeights = glm::vec4(0.7f, 0.3f, 0.0f, 0.0f);
        vertices.push_back(v);
    }

    // Left arm vertices (weighted to LeftUpperArm=4, LeftLowerArm=5)
    for (int i = 0; i < 5; ++i)
    {
        Vertex v;
        v.position = glm::vec3(-0.5f - static_cast<float>(i) * 0.2f, 1.4f, 0.0f);
        v.normal = glm::vec3(0, 0, 1);
        v.boneIds = glm::ivec4(4, 5, 0, 0);
        v.boneWeights = glm::vec4(0.6f, 0.4f, 0.0f, 0.0f);
        vertices.push_back(v);
    }

    return vertices;
}

// ============================================================
// RAGDOLL PRESET TESTS
// ============================================================

class RagdollPresetTest : public ::testing::Test {};

TEST_F(RagdollPresetTest, CreateHumanoidPreset)
{
    RagdollPreset preset = RagdollPreset::createHumanoid();

    EXPECT_EQ(preset.name, "Humanoid");
    EXPECT_EQ(preset.joints.size(), 16u);

    // Check first joint is Hips
    EXPECT_EQ(preset.joints[0].boneName, "Hips");
    EXPECT_GT(preset.joints[0].mass, 0.0f);
}

TEST_F(RagdollPresetTest, CreateSimplePreset)
{
    auto skeleton = createTestSkeleton();
    RagdollPreset preset = RagdollPreset::createSimple(*skeleton);

    EXPECT_EQ(preset.name, "Simple");
    EXPECT_EQ(preset.joints.size(), 16u);

    // All joints should have capsule shape
    for (const auto& joint : preset.joints)
    {
        EXPECT_EQ(joint.shapeType, RagdollShapeType::CAPSULE);
        EXPECT_GT(joint.mass, 0.0f);
    }
}

TEST_F(RagdollPresetTest, HumanoidJointLimitsAreReasonable)
{
    RagdollPreset preset = RagdollPreset::createHumanoid();

    for (const auto& joint : preset.joints)
    {
        // Swing limits should be non-negative
        EXPECT_GE(joint.normalHalfCone, 0.0f);
        EXPECT_GE(joint.planeHalfCone, 0.0f);

        // Twist limits should have min <= max
        EXPECT_LE(joint.twistMin, joint.twistMax);

        // Mass should be positive
        EXPECT_GT(joint.mass, 0.0f);
    }
}

TEST_F(RagdollPresetTest, PresetBoneNamesMatchSkeleton)
{
    auto skeleton = createTestSkeleton();
    RagdollPreset preset = RagdollPreset::createHumanoid();

    int matched = 0;
    for (const auto& joint : preset.joints)
    {
        if (skeleton->findJoint(joint.boneName) >= 0)
        {
            ++matched;
        }
    }

    EXPECT_EQ(matched, 16);
}

// ============================================================
// RAGDOLL TESTS (no physics system — just construction)
// ============================================================

class RagdollTest : public ::testing::Test {};

TEST_F(RagdollTest, DefaultState)
{
    Ragdoll ragdoll;
    EXPECT_FALSE(ragdoll.isCreated());
    EXPECT_EQ(ragdoll.getState(), RagdollState::INACTIVE);
    EXPECT_EQ(ragdoll.getBodyCount(), 0);
    EXPECT_FALSE(ragdoll.isPhysicsActive());
}

TEST_F(RagdollTest, DestroyWithoutCreate)
{
    Ragdoll ragdoll;
    ragdoll.destroy();  // Should not crash
    EXPECT_FALSE(ragdoll.isCreated());
}

TEST_F(RagdollTest, GetBodyIdForInvalidJoint)
{
    Ragdoll ragdoll;
    JPH::BodyID body = ragdoll.getBodyIdForJoint(0);
    EXPECT_TRUE(body.IsInvalid());
}

TEST_F(RagdollTest, RootTransformDefaultValues)
{
    Ragdoll ragdoll;
    glm::vec3 pos = ragdoll.getRootPosition();
    EXPECT_FLOAT_EQ(pos.x, 0.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.0f);
    EXPECT_FLOAT_EQ(pos.z, 0.0f);

    glm::quat rot = ragdoll.getRootRotation();
    EXPECT_FLOAT_EQ(rot.w, 1.0f);
    EXPECT_FLOAT_EQ(rot.x, 0.0f);
}

TEST_F(RagdollTest, MotorStrengthClamp)
{
    Ragdoll ragdoll;
    ragdoll.setMotorStrength(2.0f);
    EXPECT_FLOAT_EQ(ragdoll.getMotorStrength(), 1.0f);

    ragdoll.setMotorStrength(-1.0f);
    EXPECT_FLOAT_EQ(ragdoll.getMotorStrength(), 0.0f);

    ragdoll.setMotorStrength(0.5f);
    EXPECT_FLOAT_EQ(ragdoll.getMotorStrength(), 0.5f);
}

// ============================================================
// INTERACTABLE COMPONENT TESTS
// ============================================================

class InteractableComponentTest : public ::testing::Test {};

TEST_F(InteractableComponentTest, DefaultValues)
{
    InteractableComponent comp;
    EXPECT_EQ(comp.type, InteractionType::GRAB);
    EXPECT_FLOAT_EQ(comp.maxGrabMass, 50.0f);
    EXPECT_FLOAT_EQ(comp.throwForce, 10.0f);
    EXPECT_FLOAT_EQ(comp.grabDistance, 3.0f);
    EXPECT_FLOAT_EQ(comp.holdDistance, 1.5f);
    EXPECT_FALSE(comp.highlighted);
}

TEST_F(InteractableComponentTest, Clone)
{
    InteractableComponent comp;
    comp.type = InteractionType::PUSH;
    comp.throwForce = 20.0f;
    comp.promptText = "Push Me";

    auto cloned = comp.clone();
    auto* clonedComp = dynamic_cast<InteractableComponent*>(cloned.get());
    ASSERT_NE(clonedComp, nullptr);

    EXPECT_EQ(clonedComp->type, InteractionType::PUSH);
    EXPECT_FLOAT_EQ(clonedComp->throwForce, 20.0f);
    EXPECT_EQ(clonedComp->promptText, "Push Me");
}

TEST_F(InteractableComponentTest, InteractionTypes)
{
    InteractableComponent grab;
    grab.type = InteractionType::GRAB;
    EXPECT_EQ(grab.type, InteractionType::GRAB);

    InteractableComponent push;
    push.type = InteractionType::PUSH;
    EXPECT_EQ(push.type, InteractionType::PUSH);

    InteractableComponent toggle;
    toggle.type = InteractionType::TOGGLE;
    EXPECT_EQ(toggle.type, InteractionType::TOGGLE);
}

// ============================================================
// GRAB SYSTEM TESTS
// ============================================================

class GrabSystemTest : public ::testing::Test {};

TEST_F(GrabSystemTest, DefaultState)
{
    GrabSystem system;
    EXPECT_FALSE(system.isHolding());
    EXPECT_EQ(system.getHeldEntity(), nullptr);
    EXPECT_EQ(system.getLookedAtEntity(), nullptr);
}

TEST_F(GrabSystemTest, MaxLookDistance)
{
    GrabSystem system;
    EXPECT_FLOAT_EQ(system.maxLookDistance, 5.0f);

    system.maxLookDistance = 10.0f;
    EXPECT_FLOAT_EQ(system.maxLookDistance, 10.0f);
}

// ============================================================
// FRACTURE TESTS
// ============================================================

class FractureTest : public ::testing::Test {};

TEST_F(FractureTest, GenerateSeeds)
{
    AABB bounds;
    bounds.min = glm::vec3(-1, -1, -1);
    bounds.max = glm::vec3(1, 1, 1);

    auto seeds = Fracture::generateSeeds(bounds, 10, glm::vec3(0, 0, 0), 42);

    EXPECT_EQ(seeds.size(), 10u);

    // All seeds should be within bounds
    for (const auto& s : seeds)
    {
        EXPECT_GE(s.x, -1.0f);
        EXPECT_LE(s.x, 1.0f);
        EXPECT_GE(s.y, -1.0f);
        EXPECT_LE(s.y, 1.0f);
        EXPECT_GE(s.z, -1.0f);
        EXPECT_LE(s.z, 1.0f);
    }
}

TEST_F(FractureTest, GenerateSeedsReproducible)
{
    AABB bounds;
    bounds.min = glm::vec3(-1, -1, -1);
    bounds.max = glm::vec3(1, 1, 1);

    auto seeds1 = Fracture::generateSeeds(bounds, 5, glm::vec3(0.5f, 0, 0), 123);
    auto seeds2 = Fracture::generateSeeds(bounds, 5, glm::vec3(0.5f, 0, 0), 123);

    ASSERT_EQ(seeds1.size(), seeds2.size());
    for (size_t i = 0; i < seeds1.size(); ++i)
    {
        EXPECT_FLOAT_EQ(seeds1[i].x, seeds2[i].x);
        EXPECT_FLOAT_EQ(seeds1[i].y, seeds2[i].y);
        EXPECT_FLOAT_EQ(seeds1[i].z, seeds2[i].z);
    }
}

TEST_F(FractureTest, ComputeVolume)
{
    // Simple tetrahedron: volume = 1/6 * |a . (b x c)|
    std::vector<glm::vec3> positions = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}
    };

    std::vector<uint32_t> indices = {
        0, 2, 1,  // bottom
        0, 1, 3,  // front
        0, 3, 2,  // left
        1, 2, 3   // right
    };

    float vol = Fracture::computeVolume(positions, indices);
    EXPECT_NEAR(std::abs(vol), 1.0f / 6.0f, 0.01f);
}

TEST_F(FractureTest, ComputeCentroid)
{
    std::vector<glm::vec3> points = {
        {0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {2, 2, 0}
    };

    glm::vec3 c = Fracture::computeCentroid(points);
    EXPECT_FLOAT_EQ(c.x, 1.0f);
    EXPECT_FLOAT_EQ(c.y, 1.0f);
    EXPECT_FLOAT_EQ(c.z, 0.0f);
}

TEST_F(FractureTest, FractureConvexProducesFragments)
{
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    createBoxVerticesAndIndices(positions, indices);

    auto result = Fracture::fractureConvex(
        positions, indices, 4, glm::vec3(0, 0, 0), 42);

    EXPECT_TRUE(result.success);
    EXPECT_GE(static_cast<int>(result.fragments.size()), 2);

    // Each fragment should have positive volume
    for (const auto& frag : result.fragments)
    {
        EXPECT_GT(frag.volume, 0.0f);
        EXPECT_FALSE(frag.positions.empty());
        EXPECT_FALSE(frag.indices.empty());
    }
}

TEST_F(FractureTest, FractureWithTooFewFragments)
{
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    createBoxVerticesAndIndices(positions, indices);

    auto result = Fracture::fractureConvex(
        positions, indices, 1, glm::vec3(0, 0, 0), 42);

    // 1 fragment is below minimum — should fail
    EXPECT_FALSE(result.success);
}

TEST_F(FractureTest, FractureEmptyMesh)
{
    auto result = Fracture::fractureConvex({}, {}, 4, glm::vec3(0), 0);
    EXPECT_FALSE(result.success);
}

// ============================================================
// DEFORMABLE MESH TESTS
// ============================================================

class DeformableMeshTest : public ::testing::Test {};

TEST_F(DeformableMeshTest, ApplyImpactModifiesVertices)
{
    std::vector<Vertex> vertices;
    for (int i = 0; i < 5; ++i)
    {
        Vertex v;
        v.position = glm::vec3(static_cast<float>(i) * 0.1f, 0, 0);
        v.normal = glm::vec3(0, 1, 0);
        vertices.push_back(v);
    }

    std::vector<glm::vec3> original;
    for (const auto& v : vertices)
    {
        original.push_back(v.position);
    }

    DeformableMesh::applyImpact(vertices, glm::vec3(0.2f, 0, 0),
                                 glm::vec3(0, -1, 0), 0.5f, 0.1f);

    // At least some vertices should have moved
    bool anyMoved = false;
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        if (glm::distance(vertices[i].position, original[i]) > 0.0001f)
        {
            anyMoved = true;
            break;
        }
    }
    EXPECT_TRUE(anyMoved);
}

TEST_F(DeformableMeshTest, ApplyImpactZeroRadius)
{
    std::vector<Vertex> vertices(3);
    vertices[0].position = glm::vec3(0, 0, 0);
    vertices[1].position = glm::vec3(1, 0, 0);
    vertices[2].position = glm::vec3(0, 1, 0);

    auto origPos = vertices[0].position;

    DeformableMesh::applyImpact(vertices, glm::vec3(0), glm::vec3(0, -1, 0), 0.0f, 0.1f);

    // Zero radius — nothing should move
    EXPECT_FLOAT_EQ(vertices[0].position.x, origPos.x);
    EXPECT_FLOAT_EQ(vertices[0].position.y, origPos.y);
}

TEST_F(DeformableMeshTest, RecalculateNormals)
{
    std::vector<Vertex> vertices(3);
    vertices[0].position = glm::vec3(0, 0, 0);
    vertices[1].position = glm::vec3(1, 0, 0);
    vertices[2].position = glm::vec3(0, 1, 0);
    vertices[0].normal = glm::vec3(0);
    vertices[1].normal = glm::vec3(0);
    vertices[2].normal = glm::vec3(0);

    std::vector<uint32_t> indices = {0, 1, 2};

    DeformableMesh::recalculateNormals(vertices, indices);

    // Normal should be (0, 0, 1) for this triangle
    for (const auto& v : vertices)
    {
        EXPECT_NEAR(v.normal.z, 1.0f, 0.01f);
        EXPECT_NEAR(v.normal.x, 0.0f, 0.01f);
        EXPECT_NEAR(v.normal.y, 0.0f, 0.01f);
    }
}

// ============================================================
// BREAKABLE COMPONENT TESTS
// ============================================================

class BreakableComponentTest : public ::testing::Test {};

TEST_F(BreakableComponentTest, DefaultValues)
{
    BreakableComponent comp;
    EXPECT_FALSE(comp.isFractured());
    EXPECT_FLOAT_EQ(comp.breakImpulse, 50.0f);
    EXPECT_EQ(comp.fragmentCount, 8);
    EXPECT_FLOAT_EQ(comp.fragmentLifetime, 5.0f);
}

TEST_F(BreakableComponentTest, Fracture)
{
    BreakableComponent comp;
    EXPECT_FALSE(comp.isFractured());

    comp.fracture(glm::vec3(1, 0, 0), glm::vec3(0, 0, 10));
    EXPECT_TRUE(comp.isFractured());
}

TEST_F(BreakableComponentTest, FractureOnlyOnce)
{
    BreakableComponent comp;
    comp.fracture(glm::vec3(1, 0, 0), glm::vec3(0, 0, 10));
    EXPECT_TRUE(comp.isFractured());

    // Second fracture should be a no-op
    comp.fracture(glm::vec3(2, 0, 0), glm::vec3(0, 0, 20));
    EXPECT_TRUE(comp.isFractured());
}

TEST_F(BreakableComponentTest, Clone)
{
    BreakableComponent comp;
    comp.breakImpulse = 100.0f;
    comp.fragmentCount = 12;
    comp.interiorMaterial = "concrete_interior";

    auto cloned = comp.clone();
    auto* clonedComp = dynamic_cast<BreakableComponent*>(cloned.get());
    ASSERT_NE(clonedComp, nullptr);

    EXPECT_FLOAT_EQ(clonedComp->breakImpulse, 100.0f);
    EXPECT_EQ(clonedComp->fragmentCount, 12);
    EXPECT_EQ(clonedComp->interiorMaterial, "concrete_interior");
    EXPECT_FALSE(clonedComp->isFractured());
}

TEST_F(BreakableComponentTest, PrecomputeFragments)
{
    BreakableComponent comp;
    comp.fragmentCount = 4;

    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    createBoxVerticesAndIndices(positions, indices);

    comp.precomputeFragments(positions, indices);

    EXPECT_TRUE(comp.hasPrecomputedFragments());
    EXPECT_GE(static_cast<int>(comp.getFragments().size()), 2);
}

// ============================================================
// DISMEMBERMENT ZONES TESTS
// ============================================================

class DismembermentZonesTest : public ::testing::Test {};

TEST_F(DismembermentZonesTest, AddZone)
{
    DismembermentZones zones;
    DismembermentZone zone;
    zone.boneIndex = 4;
    zone.zoneName = "LeftArm";
    zone.health = 80.0f;
    zone.maxHealth = 80.0f;

    int idx = zones.addZone(zone);
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(zones.getZoneCount(), 1);
    EXPECT_EQ(zones.getZone(0).zoneName, "LeftArm");
}

TEST_F(DismembermentZonesTest, ApplyDamageAndSever)
{
    DismembermentZones zones;
    DismembermentZone zone;
    zone.boneIndex = 4;
    zone.zoneName = "LeftArm";
    zone.health = 50.0f;
    zone.maxHealth = 50.0f;

    zones.addZone(zone);

    // Apply some damage — not enough to sever
    EXPECT_FALSE(zones.applyDamage(0, 30.0f));
    EXPECT_FALSE(zones.isZoneSevered(0));
    EXPECT_NEAR(zones.getZone(0).health, 20.0f, 0.01f);

    // Apply enough damage to sever
    EXPECT_TRUE(zones.applyDamage(0, 25.0f));
    EXPECT_TRUE(zones.isZoneSevered(0));
    EXPECT_FLOAT_EQ(zones.getZone(0).health, 0.0f);
}

TEST_F(DismembermentZonesTest, ChildCascade)
{
    DismembermentZones zones;

    DismembermentZone upperArm;
    upperArm.boneIndex = 4;
    upperArm.zoneName = "UpperArm";
    upperArm.health = 50.0f;
    upperArm.maxHealth = 50.0f;
    int upperIdx = zones.addZone(upperArm);

    DismembermentZone lowerArm;
    lowerArm.boneIndex = 5;
    lowerArm.zoneName = "LowerArm";
    lowerArm.health = 50.0f;
    lowerArm.maxHealth = 50.0f;
    int lowerIdx = zones.addZone(lowerArm);

    DismembermentZone hand;
    hand.boneIndex = 6;
    hand.zoneName = "Hand";
    hand.health = 50.0f;
    hand.maxHealth = 50.0f;
    int handIdx = zones.addZone(hand);

    // Set child relationships
    zones.getZone(upperIdx).childZones = {lowerIdx, handIdx};
    zones.getZone(lowerIdx).childZones = {handIdx};

    // Sever the upper arm — should cascade to lower arm and hand
    zones.applyDamage(upperIdx, 100.0f);
    EXPECT_TRUE(zones.isZoneSevered(upperIdx));
    EXPECT_TRUE(zones.isZoneSevered(lowerIdx));
    EXPECT_TRUE(zones.isZoneSevered(handIdx));
}

TEST_F(DismembermentZonesTest, FindZoneForBone)
{
    DismembermentZones zones;
    DismembermentZone zone;
    zone.boneIndex = 7;
    zones.addZone(zone);

    EXPECT_EQ(zones.findZoneForBone(7), 0);
    EXPECT_EQ(zones.findZoneForBone(99), -1);
}

TEST_F(DismembermentZonesTest, Reset)
{
    DismembermentZones zones;
    DismembermentZone zone;
    zone.health = 50.0f;
    zone.maxHealth = 50.0f;
    zones.addZone(zone);

    zones.applyDamage(0, 100.0f);
    EXPECT_TRUE(zones.isZoneSevered(0));

    zones.reset();
    EXPECT_FALSE(zones.isZoneSevered(0));
    EXPECT_FLOAT_EQ(zones.getZone(0).health, 50.0f);
}

TEST_F(DismembermentZonesTest, CreateHumanoid)
{
    auto skeleton = createTestSkeleton();
    auto zones = DismembermentZones::createHumanoid(*skeleton);

    // Should have zones for arms, legs, and head
    EXPECT_GE(zones.getZoneCount(), 10);

    // Check that bone indices are valid
    for (int i = 0; i < zones.getZoneCount(); ++i)
    {
        EXPECT_GE(zones.getZone(i).boneIndex, 0);
        EXPECT_LT(zones.getZone(i).boneIndex, skeleton->getJointCount());
    }
}

TEST_F(DismembermentZonesTest, DamageVisualScale)
{
    DismembermentZones zones;
    DismembermentZone zone;
    zone.health = 100.0f;
    zone.maxHealth = 100.0f;
    zones.addZone(zone);

    EXPECT_FLOAT_EQ(zones.getZone(0).damageVisualScale, 0.0f);

    zones.applyDamage(0, 50.0f);
    EXPECT_NEAR(zones.getZone(0).damageVisualScale, 0.5f, 0.01f);

    zones.applyDamage(0, 50.0f);
    EXPECT_FLOAT_EQ(zones.getZone(0).damageVisualScale, 1.0f);
}

// ============================================================
// DISMEMBERMENT TESTS
// ============================================================

class DismembermentTest : public ::testing::Test {};

TEST_F(DismembermentTest, ClassifyVerticesByBoneWeight)
{
    auto skeleton = createTestSkeleton();
    auto vertices = createSkinnedVertices();

    std::vector<int> side;
    Dismemberment::classifyVertices(
        vertices, 4, *skeleton, 0.1f, side);

    ASSERT_EQ(side.size(), vertices.size());

    // Body vertices (indices 0-4) should be on body side (0)
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(side[static_cast<size_t>(i)], 0) << "Vertex " << i;
    }

    // Arm vertices (indices 5-9) should be on limb side (1)
    for (int i = 5; i < 10; ++i)
    {
        EXPECT_EQ(side[static_cast<size_t>(i)], 1) << "Vertex " << i;
    }
}

TEST_F(DismembermentTest, SplitAtBoneProducesResult)
{
    auto skeleton = createTestSkeleton();
    auto vertices = createSkinnedVertices();
    auto boneMatrices = createIdentityBoneMatrices();

    // Create some triangle indices (simplified)
    std::vector<uint32_t> indices;
    // Body triangles
    indices.push_back(0); indices.push_back(1); indices.push_back(2);
    indices.push_back(2); indices.push_back(3); indices.push_back(4);
    // Arm triangles
    indices.push_back(5); indices.push_back(6); indices.push_back(7);
    indices.push_back(7); indices.push_back(8); indices.push_back(9);

    DismembermentZone zone;
    zone.boneIndex = 4;  // LeftUpperArm
    zone.cutPlaneNormal = glm::vec3(1, 0, 0);

    auto result = Dismemberment::splitAtBone(
        vertices, indices, zone, *skeleton, boneMatrices);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.bodyVertices.empty());
    EXPECT_FALSE(result.limbVertices.empty());
    EXPECT_FALSE(result.bodyIndices.empty());
    EXPECT_FALSE(result.limbIndices.empty());
}

TEST_F(DismembermentTest, SplitEmptyMesh)
{
    auto skeleton = createTestSkeleton();
    DismembermentZone zone;
    zone.boneIndex = 4;

    auto result = Dismemberment::splitAtBone(
        {}, {}, zone, *skeleton, {});

    EXPECT_FALSE(result.success);
}

// ============================================================
// INTEGRATION TESTS
// ============================================================

class AdvancedPhysicsIntegration : public ::testing::Test {};

TEST_F(AdvancedPhysicsIntegration, RagdollPresetMatchesSkeleton)
{
    auto skeleton = createTestSkeleton();
    RagdollPreset preset = RagdollPreset::createHumanoid();

    // Every preset bone name should exist in the skeleton
    for (const auto& joint : preset.joints)
    {
        EXPECT_GE(skeleton->findJoint(joint.boneName), 0)
            << "Bone '" << joint.boneName << "' not found in skeleton";
    }
}

TEST_F(AdvancedPhysicsIntegration, DismembermentZonesMatchSkeleton)
{
    auto skeleton = createTestSkeleton();
    auto zones = DismembermentZones::createHumanoid(*skeleton);

    for (int i = 0; i < zones.getZoneCount(); ++i)
    {
        const auto& zone = zones.getZone(i);
        EXPECT_GE(zone.boneIndex, 0);
        EXPECT_LT(zone.boneIndex, skeleton->getJointCount());
        EXPECT_GT(zone.maxHealth, 0.0f);
    }
}

TEST_F(AdvancedPhysicsIntegration, FractureAndMeasureFragments)
{
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    createBoxVerticesAndIndices(positions, indices);

    auto result = Fracture::fractureConvex(
        positions, indices, 4, glm::vec3(0.3f, 0.3f, 0), 42);

    EXPECT_TRUE(result.success);

    // Total fragment volume should approximately equal original volume
    float totalVolume = 0.0f;
    for (const auto& frag : result.fragments)
    {
        totalVolume += frag.volume;
    }

    // Original box volume = 1.0 (unit cube)
    // Fragments may not perfectly sum due to Voronoi approximation
    EXPECT_GT(totalVolume, 0.1f);
}
