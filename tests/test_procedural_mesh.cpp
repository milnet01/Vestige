/// @file test_procedural_mesh.cpp
/// @brief Unit tests for ProceduralMeshBuilder, align/distribute, and import dialog.

#include "utils/procedural_mesh.h"
#include "editor/entity_actions.h"
#include "editor/command_history.h"
#include "editor/selection.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

#include <limits>

using namespace Vestige;

// =============================================================================
// Helper: compute AABB from vertex data
// =============================================================================

namespace
{

struct TestBounds
{
    glm::vec3 min;
    glm::vec3 max;

    glm::vec3 getSize() const { return max - min; }
    glm::vec3 getCenter() const { return (min + max) * 0.5f; }
};

TestBounds computeBounds(const ProceduralMeshData& data)
{
    TestBounds b;
    b.min = glm::vec3(std::numeric_limits<float>::max());
    b.max = glm::vec3(std::numeric_limits<float>::lowest());
    for (const auto& v : data.vertices)
    {
        b.min = glm::min(b.min, v.position);
        b.max = glm::max(b.max, v.position);
    }
    return b;
}

} // anonymous namespace

// =============================================================================
// Wall tests
// =============================================================================

TEST(ProceduralMeshTest, CreateWallProducesGeometry)
{
    auto data = ProceduralMeshBuilder::generateWall(3.0f, 3.0f, 0.3f);
    EXPECT_GT(data.indices.size(), 0u);
    EXPECT_GT(data.vertices.size(), 0u);

    auto bounds = computeBounds(data);
    auto size = bounds.getSize();
    EXPECT_NEAR(size.x, 3.0f, 0.01f);
    EXPECT_NEAR(size.y, 3.0f, 0.01f);
    EXPECT_NEAR(size.z, 0.3f, 0.01f);
}

TEST(ProceduralMeshTest, CreateWallDifferentSizes)
{
    auto data = ProceduralMeshBuilder::generateWall(5.0f, 2.0f, 0.5f);
    auto size = computeBounds(data).getSize();
    EXPECT_NEAR(size.x, 5.0f, 0.01f);
    EXPECT_NEAR(size.y, 2.0f, 0.01f);
    EXPECT_NEAR(size.z, 0.5f, 0.01f);
}

TEST(ProceduralMeshTest, CreateWallCenteredAtOrigin)
{
    auto data = ProceduralMeshBuilder::generateWall(4.0f, 3.0f, 0.3f);
    auto center = computeBounds(data).getCenter();
    EXPECT_NEAR(center.x, 0.0f, 0.01f);
    EXPECT_NEAR(center.y, 1.5f, 0.01f);
    EXPECT_NEAR(center.z, 0.0f, 0.01f);
}

// =============================================================================
// Wall with openings tests
// =============================================================================

TEST(ProceduralMeshTest, WallWithNoOpeningsSameAsSolid)
{
    auto solid = ProceduralMeshBuilder::generateWall(3.0f, 3.0f, 0.3f);
    auto noOpenings = ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, {});
    EXPECT_EQ(solid.indices.size(), noOpenings.indices.size());
}

TEST(ProceduralMeshTest, WallWithDoorOpening)
{
    std::vector<WallOpening> openings;
    openings.push_back({1.0f, 0.0f, 1.0f, 2.1f});

    auto wall = ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, openings);
    EXPECT_GT(wall.indices.size(), 0u);

    auto solid = ProceduralMeshBuilder::generateWall(3.0f, 3.0f, 0.3f);
    EXPECT_GT(wall.indices.size(), solid.indices.size());

    auto size = computeBounds(wall).getSize();
    EXPECT_NEAR(size.x, 3.0f, 0.01f);
    EXPECT_NEAR(size.y, 3.0f, 0.01f);
    EXPECT_NEAR(size.z, 0.3f, 0.01f);
}

TEST(ProceduralMeshTest, WallWithWindowOpening)
{
    std::vector<WallOpening> openings;
    openings.push_back({0.75f, 0.9f, 1.5f, 1.2f});

    auto wall = ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, openings);
    EXPECT_GT(wall.indices.size(), 0u);

    // Window has a sill (bottom reveal) so more faces than a door
    std::vector<WallOpening> doorOpening;
    doorOpening.push_back({1.0f, 0.0f, 1.0f, 2.1f});
    auto doorWall = ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, doorOpening);
    EXPECT_GT(wall.indices.size(), doorWall.indices.size());
}

TEST(ProceduralMeshTest, WallWithMultipleOpenings)
{
    std::vector<WallOpening> openings;
    openings.push_back({0.2f, 0.0f, 0.8f, 2.1f});
    openings.push_back({1.5f, 0.9f, 0.8f, 1.0f});

    auto wall = ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, openings);
    EXPECT_GT(wall.indices.size(), 0u);

    std::vector<WallOpening> singleOpening;
    singleOpening.push_back({1.0f, 0.0f, 1.0f, 2.1f});
    auto singleWall = ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, singleOpening);
    EXPECT_GT(wall.indices.size(), singleWall.indices.size());
}

TEST(ProceduralMeshTest, WallOpeningClampedToBounds)
{
    std::vector<WallOpening> openings;
    openings.push_back({2.5f, 0.0f, 2.0f, 5.0f});

    auto wall = ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, openings);
    EXPECT_GT(wall.indices.size(), 0u);
}

// =============================================================================
// Floor tests
// =============================================================================

TEST(ProceduralMeshTest, CreateFloor)
{
    auto data = ProceduralMeshBuilder::generateFloor(4.0f, 4.0f, 0.15f);
    EXPECT_GT(data.indices.size(), 0u);

    auto size = computeBounds(data).getSize();
    EXPECT_NEAR(size.x, 4.0f, 0.01f);
    EXPECT_NEAR(size.z, 4.0f, 0.01f);
    EXPECT_NEAR(size.y, 0.15f, 0.01f);
}

TEST(ProceduralMeshTest, FloorTopAtYZero)
{
    auto data = ProceduralMeshBuilder::generateFloor(4.0f, 4.0f, 0.15f);
    auto bounds = computeBounds(data);
    EXPECT_NEAR(bounds.max.y, 0.0f, 0.01f);
    EXPECT_NEAR(bounds.min.y, -0.15f, 0.01f);
}

// =============================================================================
// Roof tests
// =============================================================================

TEST(ProceduralMeshTest, FlatRoof)
{
    auto data = ProceduralMeshBuilder::generateRoof(RoofType::FLAT, 4.0f, 4.0f, 3.0f, 0.3f);
    EXPECT_GT(data.indices.size(), 0u);

    auto size = computeBounds(data).getSize();
    EXPECT_NEAR(size.x, 4.6f, 0.01f);
    EXPECT_NEAR(size.z, 4.6f, 0.01f);
}

TEST(ProceduralMeshTest, GableRoof)
{
    auto data = ProceduralMeshBuilder::generateRoof(RoofType::GABLE, 4.0f, 4.0f, 1.5f, 0.3f);
    EXPECT_GT(data.indices.size(), 0u);

    auto bounds = computeBounds(data);
    EXPECT_NEAR(bounds.max.y, 1.5f, 0.01f);
    EXPECT_NEAR(bounds.min.y, 0.0f, 0.01f);
}

TEST(ProceduralMeshTest, ShedRoof)
{
    auto data = ProceduralMeshBuilder::generateRoof(RoofType::SHED, 4.0f, 4.0f, 2.0f, 0.3f);
    EXPECT_GT(data.indices.size(), 0u);

    auto bounds = computeBounds(data);
    EXPECT_NEAR(bounds.max.y, 2.0f, 0.01f);
}

TEST(ProceduralMeshTest, RoofOverhang)
{
    float overhang = 0.5f;
    auto data = ProceduralMeshBuilder::generateRoof(RoofType::GABLE, 4.0f, 6.0f, 1.5f, overhang);
    auto size = computeBounds(data).getSize();
    EXPECT_NEAR(size.x, 5.0f, 0.01f);
    EXPECT_NEAR(size.z, 7.0f, 0.01f);
}

// =============================================================================
// Stair tests
// =============================================================================

TEST(ProceduralMeshTest, StraightStairs)
{
    auto data = ProceduralMeshBuilder::generateStraightStairs(3.0f, 0.178f, 0.279f, 1.0f);
    EXPECT_GT(data.indices.size(), 0u);

    auto bounds = computeBounds(data);
    EXPECT_NEAR(bounds.max.y, 3.0f, 0.1f);
    auto size = bounds.getSize();
    EXPECT_NEAR(size.x, 1.0f, 0.01f);
}

TEST(ProceduralMeshTest, StraightStairsStepCount)
{
    auto data = ProceduralMeshBuilder::generateStraightStairs(3.0f, 0.178f, 0.279f, 1.0f);
    auto bounds = computeBounds(data);
    float expectedDepth = 17.0f * 0.279f;
    EXPECT_NEAR(bounds.max.z, expectedDepth, 0.5f);
}

TEST(ProceduralMeshTest, StraightStairsStartsAtOrigin)
{
    auto data = ProceduralMeshBuilder::generateStraightStairs(2.0f, 0.2f, 0.3f, 1.0f);
    auto bounds = computeBounds(data);
    EXPECT_NEAR(bounds.min.y, 0.0f, 0.01f);
    EXPECT_NEAR(bounds.min.z, 0.0f, 0.01f);
}

TEST(ProceduralMeshTest, SpiralStairs)
{
    auto data = ProceduralMeshBuilder::generateSpiralStairs(3.0f, 0.2f, 0.2f, 1.2f, 360.0f);
    EXPECT_GT(data.indices.size(), 0u);

    auto bounds = computeBounds(data);
    EXPECT_NEAR(bounds.max.y, 3.0f, 0.1f);
    auto size = bounds.getSize();
    EXPECT_GT(size.x, 1.0f);
    EXPECT_GT(size.z, 1.0f);
}

TEST(ProceduralMeshTest, SpiralStairsPartialTurn)
{
    auto data = ProceduralMeshBuilder::generateSpiralStairs(1.5f, 0.2f, 0.2f, 1.0f, 180.0f);
    EXPECT_GT(data.indices.size(), 0u);
    auto bounds = computeBounds(data);
    EXPECT_NEAR(bounds.max.y, 1.5f, 0.1f);
}

TEST(ProceduralMeshTest, AllVerticesHaveNormals)
{
    auto data = ProceduralMeshBuilder::generateWall(3.0f, 3.0f, 0.3f);
    for (const auto& v : data.vertices)
    {
        float len = glm::length(v.normal);
        EXPECT_NEAR(len, 1.0f, 0.01f);
    }
}

TEST(ProceduralMeshTest, AllVerticesHaveTangents)
{
    auto data = ProceduralMeshBuilder::generateWall(3.0f, 3.0f, 0.3f);
    bool hasTangent = false;
    for (const auto& v : data.vertices)
    {
        if (glm::length(v.tangent) > 0.01f) hasTangent = true;
    }
    EXPECT_TRUE(hasTangent);
}

// =============================================================================
// Align/Distribute tests
// =============================================================================

TEST(AlignDistributeTest, AlignMinX)
{
    Scene scene("Test");
    Selection sel;
    CommandHistory history;

    Entity* e1 = scene.createEntity("A");
    e1->transform.position = glm::vec3(5.0f, 0.0f, 0.0f);
    Entity* e2 = scene.createEntity("B");
    e2->transform.position = glm::vec3(10.0f, 0.0f, 0.0f);
    Entity* e3 = scene.createEntity("C");
    e3->transform.position = glm::vec3(2.0f, 0.0f, 0.0f);

    sel.select(e1->getId());
    sel.addToSelection(e2->getId());
    sel.addToSelection(e3->getId());

    EntityActions::alignEntities(scene, sel, history,
        EntityActions::AlignAxis::X, EntityActions::AlignAnchor::MIN);

    EXPECT_FLOAT_EQ(e1->transform.position.x, 2.0f);
    EXPECT_FLOAT_EQ(e2->transform.position.x, 2.0f);
    EXPECT_FLOAT_EQ(e3->transform.position.x, 2.0f);
    EXPECT_FLOAT_EQ(e1->transform.position.y, 0.0f);
}

TEST(AlignDistributeTest, AlignMaxY)
{
    Scene scene("Test");
    Selection sel;
    CommandHistory history;

    Entity* e1 = scene.createEntity("A");
    e1->transform.position = glm::vec3(0.0f, 3.0f, 0.0f);
    Entity* e2 = scene.createEntity("B");
    e2->transform.position = glm::vec3(0.0f, 7.0f, 0.0f);

    sel.select(e1->getId());
    sel.addToSelection(e2->getId());

    EntityActions::alignEntities(scene, sel, history,
        EntityActions::AlignAxis::Y, EntityActions::AlignAnchor::MAX);

    EXPECT_FLOAT_EQ(e1->transform.position.y, 7.0f);
    EXPECT_FLOAT_EQ(e2->transform.position.y, 7.0f);
}

TEST(AlignDistributeTest, AlignCenterZ)
{
    Scene scene("Test");
    Selection sel;
    CommandHistory history;

    Entity* e1 = scene.createEntity("A");
    e1->transform.position = glm::vec3(0.0f, 0.0f, 2.0f);
    Entity* e2 = scene.createEntity("B");
    e2->transform.position = glm::vec3(0.0f, 0.0f, 8.0f);

    sel.select(e1->getId());
    sel.addToSelection(e2->getId());

    EntityActions::alignEntities(scene, sel, history,
        EntityActions::AlignAxis::Z, EntityActions::AlignAnchor::CENTER);

    EXPECT_FLOAT_EQ(e1->transform.position.z, 5.0f);
    EXPECT_FLOAT_EQ(e2->transform.position.z, 5.0f);
}

TEST(AlignDistributeTest, AlignUndoable)
{
    Scene scene("Test");
    Selection sel;
    CommandHistory history;

    Entity* e1 = scene.createEntity("A");
    e1->transform.position = glm::vec3(5.0f, 0.0f, 0.0f);
    Entity* e2 = scene.createEntity("B");
    e2->transform.position = glm::vec3(10.0f, 0.0f, 0.0f);

    sel.select(e1->getId());
    sel.addToSelection(e2->getId());

    EntityActions::alignEntities(scene, sel, history,
        EntityActions::AlignAxis::X, EntityActions::AlignAnchor::MIN);

    EXPECT_FLOAT_EQ(e1->transform.position.x, 5.0f);
    EXPECT_FLOAT_EQ(e2->transform.position.x, 5.0f);

    history.undo();
    EXPECT_FLOAT_EQ(e1->transform.position.x, 5.0f);
    EXPECT_FLOAT_EQ(e2->transform.position.x, 10.0f);
}

TEST(AlignDistributeTest, DistributeX)
{
    Scene scene("Test");
    Selection sel;
    CommandHistory history;

    Entity* e1 = scene.createEntity("A");
    e1->transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    Entity* e2 = scene.createEntity("B");
    e2->transform.position = glm::vec3(1.0f, 0.0f, 0.0f);
    Entity* e3 = scene.createEntity("C");
    e3->transform.position = glm::vec3(10.0f, 0.0f, 0.0f);

    sel.select(e1->getId());
    sel.addToSelection(e2->getId());
    sel.addToSelection(e3->getId());

    EntityActions::distributeEntities(scene, sel, history, EntityActions::AlignAxis::X);

    EXPECT_FLOAT_EQ(e1->transform.position.x, 0.0f);
    EXPECT_NEAR(e2->transform.position.x, 5.0f, 0.01f);
    EXPECT_FLOAT_EQ(e3->transform.position.x, 10.0f);
}

TEST(AlignDistributeTest, DistributeUndoable)
{
    Scene scene("Test");
    Selection sel;
    CommandHistory history;

    Entity* e1 = scene.createEntity("A");
    e1->transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    Entity* e2 = scene.createEntity("B");
    e2->transform.position = glm::vec3(1.0f, 0.0f, 0.0f);
    Entity* e3 = scene.createEntity("C");
    e3->transform.position = glm::vec3(10.0f, 0.0f, 0.0f);

    sel.select(e1->getId());
    sel.addToSelection(e2->getId());
    sel.addToSelection(e3->getId());

    EntityActions::distributeEntities(scene, sel, history, EntityActions::AlignAxis::X);
    EXPECT_NEAR(e2->transform.position.x, 5.0f, 0.01f);

    history.undo();
    EXPECT_FLOAT_EQ(e2->transform.position.x, 1.0f);
}

TEST(AlignDistributeTest, DistributeTooFewEntities)
{
    Scene scene("Test");
    Selection sel;
    CommandHistory history;

    Entity* e1 = scene.createEntity("A");
    e1->transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    Entity* e2 = scene.createEntity("B");
    e2->transform.position = glm::vec3(10.0f, 0.0f, 0.0f);

    sel.select(e1->getId());
    sel.addToSelection(e2->getId());

    EntityActions::distributeEntities(scene, sel, history, EntityActions::AlignAxis::X);

    EXPECT_FLOAT_EQ(e1->transform.position.x, 0.0f);
    EXPECT_FLOAT_EQ(e2->transform.position.x, 10.0f);
}

TEST(AlignDistributeTest, AlignSingleEntityNoOp)
{
    Scene scene("Test");
    Selection sel;
    CommandHistory history;

    Entity* e1 = scene.createEntity("A");
    e1->transform.position = glm::vec3(5.0f, 3.0f, 7.0f);

    sel.select(e1->getId());

    EntityActions::alignEntities(scene, sel, history,
        EntityActions::AlignAxis::X, EntityActions::AlignAnchor::MIN);

    EXPECT_FLOAT_EQ(e1->transform.position.x, 5.0f);
}

// =============================================================================
// WallOpening struct tests
// =============================================================================

TEST(WallOpeningTest, DefaultValues)
{
    WallOpening op;
    EXPECT_FLOAT_EQ(op.xOffset, 1.0f);
    EXPECT_FLOAT_EQ(op.yOffset, 0.0f);
    EXPECT_FLOAT_EQ(op.width, 1.0f);
    EXPECT_FLOAT_EQ(op.height, 2.1f);
}
