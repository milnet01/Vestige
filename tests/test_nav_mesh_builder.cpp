// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_nav_mesh_builder.cpp
/// @brief Baking a navmesh from a scene's meshes reads their real vertex
///        positions back from the GPU (3D_E-0874).
///
/// Contract: a 20 m x 20 m floor on a MeshRenderer bakes to a navmesh lying
/// on that floor (y = 0) and reaching close to its edges. The walkable area
/// stops one agent radius short of each edge, so "close" allows 2 m.

#include "gl_test_fixture.h"
#include "navigation/nav_mesh_builder.h"
#include "renderer/mesh.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

class NavMeshBuilderTest : public GLTestFixture
{
};

TEST_F(NavMeshBuilderTest, FloorMeshBakesToNavmeshCoveringTheFloor)
{
    constexpr float HALF_SIZE = 10.0f;

    Scene scene;
    Entity* floor = scene.createEntity("Floor");
    auto* renderer = floor->addComponent<MeshRenderer>();
    renderer->setMesh(std::make_shared<Mesh>(Mesh::createPlane(HALF_SIZE)));

    NavMeshBuilder builder;
    ASSERT_TRUE(builder.buildFromScene(scene));
    ASSERT_GT(builder.getPolyCount(), 0);

    std::vector<glm::vec3> edges;
    builder.extractPolygonEdges(edges);
    ASSERT_FALSE(edges.empty());

    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const glm::vec3& p : edges)
    {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }

    EXPECT_LT(lo.x, -(HALF_SIZE - 2.0f));
    EXPECT_GT(hi.x, HALF_SIZE - 2.0f);
    EXPECT_LT(lo.z, -(HALF_SIZE - 2.0f));
    EXPECT_GT(hi.z, HALF_SIZE - 2.0f);
    EXPECT_GE(lo.x, -HALF_SIZE);
    EXPECT_LE(hi.x, HALF_SIZE);
    EXPECT_GE(lo.z, -HALF_SIZE);
    EXPECT_LE(hi.z, HALF_SIZE);
    EXPECT_NEAR(lo.y, 0.0f, 0.5f);
    EXPECT_NEAR(hi.y, 0.0f, 0.5f);
}

// Detour takes ownership of tile data only once the tile is added, so data it
// rejects is still the caller's. initNavMeshTakingData must free it; under
// ASan a leak here fails the run at exit (3D_E-0874).
TEST(NavMeshInit, RejectedTileDataIsFreed)
{
    auto* data = static_cast<unsigned char*>(
        dtAlloc(sizeof(dtMeshHeader), DT_ALLOC_PERM));
    ASSERT_NE(data, nullptr);
    std::memset(data, 0, sizeof(dtMeshHeader));  // magic 0: Detour rejects it

    dtNavMesh* navMesh = dtAllocNavMesh();
    ASSERT_NE(navMesh, nullptr);
    EXPECT_FALSE(initNavMeshTakingData(*navMesh, data,
                                       static_cast<int>(sizeof(dtMeshHeader))));
    dtFreeNavMesh(navMesh);
}
