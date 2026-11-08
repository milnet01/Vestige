// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_mesh_renderer.cpp
/// @brief A MeshRenderer's bounds follow the mesh it draws (3D_E-0874).
///
/// Contract: the bounds are the current mesh's local bounds, whether the
/// mesh came through the constructor or setMesh, and a later setBounds
/// overrides them. Zero bounds mean "never culled" to the renderer, and
/// stale bounds cull a swapped-in larger mesh too early.

#include "gl_test_fixture.h"
#include "renderer/mesh.h"
#include "scene/mesh_renderer.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

void expectBoundsEq(const AABB& actual, const AABB& expected)
{
    EXPECT_EQ(actual.min, expected.min);
    EXPECT_EQ(actual.max, expected.max);
}

} // namespace

class MeshRendererBoundsTest : public GLTestFixture
{
};

TEST_F(MeshRendererBoundsTest, ConstructorTakesBoundsFromMesh)
{
    auto cube = std::make_shared<Mesh>(Mesh::createCube());
    ASSERT_NE(cube->getLocalBounds().getSize(), glm::vec3(0.0f));

    MeshRenderer renderer(cube, nullptr);
    expectBoundsEq(renderer.getBounds(), cube->getLocalBounds());
}

TEST_F(MeshRendererBoundsTest, SetMeshReplacesThePreviousMeshsBounds)
{
    auto cube = std::make_shared<Mesh>(Mesh::createCube());
    auto plane = std::make_shared<Mesh>(Mesh::createPlane(10.0f));

    MeshRenderer renderer;
    renderer.setMesh(cube);
    renderer.setMesh(plane);
    expectBoundsEq(renderer.getBounds(), plane->getLocalBounds());
}

TEST_F(MeshRendererBoundsTest, SetBoundsAfterSetMeshOverrides)
{
    auto cube = std::make_shared<Mesh>(Mesh::createCube());
    const AABB custom{glm::vec3(-2.0f), glm::vec3(3.0f)};

    MeshRenderer renderer;
    renderer.setMesh(cube);
    renderer.setBounds(custom);
    expectBoundsEq(renderer.getBounds(), custom);
}
