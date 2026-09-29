// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_blocks_movement.cpp
/// @brief Only meshes marked "Blocks Movement" become camera/player colliders.
///
/// Every mesh used to collide, so the camera bumped into lily pads and small
/// flowers. What is worth colliding with depends on the scene (a pebble
/// matters to a rat), so it is a per-object flag, on by default.
#include "environment/foliage_manager.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

namespace Vestige
{
namespace
{

MeshRenderer* addBoxMesh(Scene& scene, const char* name)
{
    Entity* e = scene.createEntity(name);
    auto* mr = e->addComponent<MeshRenderer>();
    mr->setBounds(AABB{glm::vec3(-0.5f), glm::vec3(0.5f)});
    return mr;
}

TEST(BlocksMovement, MeshesCollideByDefault)
{
    Scene scene;
    const MeshRenderer* mr = addBoxMesh(scene, "Rock");
    EXPECT_TRUE(mr->blocksMovement());
    EXPECT_EQ(scene.collectColliders().size(), 1u);
}

TEST(BlocksMovement, ClearedFlagDropsTheCollider)
{
    Scene scene;
    addBoxMesh(scene, "Rock");
    addBoxMesh(scene, "LilyPad")->setBlocksMovement(false);
    EXPECT_EQ(scene.collectColliders().size(), 1u);
}

TEST(BlocksMovement, FlagSurvivesClone)
{
    MeshRenderer mr;
    mr.setBlocksMovement(false);
    const auto copy = mr.clone();
    const auto* cloned = dynamic_cast<MeshRenderer*>(copy.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_FALSE(cloned->blocksMovement());
}

TEST(TreeCollision, EachTreeAddsAScaledTrunkBox)
{
    FoliageManager foliage;
    TreeInstance tree;
    tree.position = glm::vec3(10.0f, 2.0f, -4.0f);
    tree.scale = 2.0f;
    foliage.placeTree(tree, 0.0f);

    std::vector<AABB> colliders;
    foliage.appendTreeTrunkColliders(colliders, 0.3f, 6.0f);

    ASSERT_EQ(colliders.size(), 1u);
    EXPECT_FLOAT_EQ(colliders[0].min.x, 9.4f);
    EXPECT_FLOAT_EQ(colliders[0].max.z, -3.4f);
    EXPECT_FLOAT_EQ(colliders[0].min.y, 2.0f);
    EXPECT_FLOAT_EQ(colliders[0].max.y, 14.0f);
}

} // namespace
} // namespace Vestige
