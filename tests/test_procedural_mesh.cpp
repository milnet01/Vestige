// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_procedural_mesh.cpp
/// @brief Unit tests for ProceduralMeshBuilder, align/distribute, and import dialog.

#include "utils/procedural_mesh.h"
#include "editor/entity_actions.h"
#include "editor/command_history.h"
#include "editor/selection.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

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
    // Slice 18 Ts1 strengthening: index counts can collide with
    // different topologies — also pin vertex count + bounds.
    EXPECT_EQ(solid.vertices.size(), noOpenings.vertices.size());
    const TestBounds solidBounds = computeBounds(solid);
    const TestBounds openBounds = computeBounds(noOpenings);
    EXPECT_NEAR(solidBounds.max.y - solidBounds.min.y,
                openBounds.max.y - openBounds.min.y, 0.001f);
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

// Slice 18 Ts1 cleanup: renamed from `WallOpeningClampedToBounds` —
// the body only asserts `indices.size() > 0`. Whether the opening was
// clamped vs silently dropped is not observable in the index count
// alone. The clamp behavior is exercised at editor launch where the
// resulting wall mesh is rendered.
TEST(ProceduralMeshTest, WallOpeningPartiallyOutsideStillProducesGeometry)
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

// Every triangle's stored normals agree with the way its winding faces
// (counter-clockwise = front). Lighting reads the stored normal and face
// culling reads the winding, so a mismatch lights a face as if it pointed
// the other way, or culls it from the side it should be seen from.
// Returns the number of triangles whose normals point up.
static int expectNormalsMatchWinding(const ProceduralMeshData& data)
{
    EXPECT_EQ(data.indices.size() % 3, 0u);
    int upward = 0;
    for (size_t t = 0; t + 2 < data.indices.size(); t += 3)
    {
        const Vertex& a = data.vertices[data.indices[t]];
        const Vertex& b = data.vertices[data.indices[t + 1]];
        const Vertex& c = data.vertices[data.indices[t + 2]];
        const glm::vec3 facing = glm::normalize(
            glm::cross(b.position - a.position, c.position - a.position));
        for (const Vertex* v : {&a, &b, &c})
        {
            EXPECT_GT(glm::dot(facing, v->normal), 0.5f)
                << "triangle starting at index " << t << " faces ("
                << facing.x << ", " << facing.y << ", " << facing.z
                << "), normal (" << v->normal.x << ", " << v->normal.y
                << ", " << v->normal.z << ")";
        }
        if (a.normal.y > 0.1f)
        {
            ++upward;
        }
    }
    return upward;
}

// Each reveal (the hole's sill, top and sides) faces into the hole, by
// winding and by normal, so it is drawn and lit when seen through it.
static void expectRevealsFaceIntoHole(const ProceduralMeshData& data,
                                      float wallWidth, const WallOpening& op)
{
    expectNormalsMatchWinding(data);

    const float x0 = op.xOffset - wallWidth * 0.5f;
    const float x1 = x0 + op.width;
    const float y0 = op.yOffset;
    const float y1 = op.yOffset + op.height;
    const glm::vec2 holeCentre((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
    const float eps = 1e-4f;

    int reveals = 0;
    for (size_t t = 0; t + 2 < data.indices.size(); t += 3)
    {
        const Vertex& a = data.vertices[data.indices[t]];
        const Vertex& b = data.vertices[data.indices[t + 1]];
        const Vertex& c = data.vertices[data.indices[t + 2]];
        if (std::abs(a.normal.z) > 0.5f)
        {
            continue;  // front or back face of the wall
        }
        bool onHoleEdge = true;
        for (const Vertex* v : {&a, &b, &c})
        {
            onHoleEdge = onHoleEdge
                && v->position.x > x0 - eps && v->position.x < x1 + eps
                && v->position.y > y0 - eps && v->position.y < y1 + eps;
        }
        if (!onHoleEdge)
        {
            continue;  // an outer edge of the wall
        }
        ++reveals;
        const glm::vec3 centroid = (a.position + b.position + c.position) / 3.0f;
        const glm::vec2 towardHole = holeCentre - glm::vec2(centroid.x, centroid.y);
        EXPECT_GT(glm::dot(glm::vec2(a.normal.x, a.normal.y), towardHole), 0.0f)
            << "reveal triangle at index " << t << " has normal ("
            << a.normal.x << ", " << a.normal.y << ", " << a.normal.z << ")";
    }
    // Sides and top always; the sill only where the hole is off the floor.
    const int expectedQuads = op.yOffset > 0.001f ? 4 : 3;
    EXPECT_EQ(reveals, expectedQuads * 2);
}

TEST(ProceduralMeshTest, WallOpeningRevealsFaceIntoTheHole)
{
    WallOpening window;
    window.xOffset = 1.0f;
    window.yOffset = 0.9f;
    window.width = 1.0f;
    window.height = 1.2f;
    expectRevealsFaceIntoHole(
        ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, {window}),
        3.0f, window);

    WallOpening door;
    door.xOffset = 1.0f;
    door.yOffset = 0.0f;
    door.width = 1.0f;
    door.height = 2.1f;
    expectRevealsFaceIntoHole(
        ProceduralMeshBuilder::generateWallWithOpenings(3.0f, 3.0f, 0.3f, {door}),
        3.0f, door);
}

TEST(ProceduralMeshTest, GableRoofSlopesFaceUpAndOut)
{
    auto data = ProceduralMeshBuilder::generateRoof(RoofType::GABLE, 4.0f, 6.0f, 1.5f, 0.3f);
    // The two slopes are the only faces with an upward normal: two quads.
    EXPECT_EQ(expectNormalsMatchWinding(data), 4);
}

TEST(ProceduralMeshTest, ShedRoofFacesPointOutward)
{
    auto data = ProceduralMeshBuilder::generateRoof(RoofType::SHED, 4.0f, 6.0f, 1.5f, 0.3f);
    // The slope is the only face with an upward normal: one quad.
    EXPECT_EQ(expectNormalsMatchWinding(data), 2);
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

// Slice 18 Ts1 cleanup: renamed from `AllVerticesHaveNormals` —
// the body actually verifies the unit-length invariant, not the
// "has a normal" invariant.
TEST(ProceduralMeshTest, AllVertexNormalsAreUnitLength)
{
    auto data = ProceduralMeshBuilder::generateWall(3.0f, 3.0f, 0.3f);
    for (const auto& v : data.vertices)
    {
        float len = glm::length(v.normal);
        EXPECT_NEAR(len, 1.0f, 0.01f);
    }
}

// Slice 18 Ts1 strengthening: the previous body short-circuited on the
// first non-zero tangent and asserted the bool — a wall where every
// vertex except the first had a zero tangent would have passed.
TEST(ProceduralMeshTest, EveryVertexHasNonZeroTangent)
{
    auto data = ProceduralMeshBuilder::generateWall(3.0f, 3.0f, 0.3f);
    for (const auto& v : data.vertices)
    {
        EXPECT_GT(glm::length(v.tangent), 0.01f);
    }
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
