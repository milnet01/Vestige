// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cutout_opening_command.cpp
/// @brief 3D_E-0838 — CutoutOpeningCommand must survive its entity being
///        deleted while it sits on the undo stack.

#include "editor/commands/cutout_opening_command.h"
#include "renderer/mesh.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

#include <memory>

using namespace Vestige;

namespace
{

struct Wall
{
    Scene scene;
    uint32_t id = 0;
    std::shared_ptr<Mesh> oldMesh = std::make_shared<Mesh>();
    std::shared_ptr<Mesh> newMesh = std::make_shared<Mesh>();

    Wall()
    {
        Entity* e = scene.createEntity("Wall");
        id = e->getId();
        e->addComponent<MeshRenderer>()->setMesh(oldMesh);
    }
};

}  // namespace

TEST(CutoutOpeningCommand, ExecuteAndUndoSwapMeshAndName)
{
    Wall w;
    CutoutOpeningCommand cmd(w.scene, w.id, w.oldMesh, w.newMesh,
                             "Wall", "Wall with Door", "cut");
    cmd.execute();
    Entity* e = w.scene.findEntityById(w.id);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->getComponent<MeshRenderer>()->getMesh(), w.newMesh);
    EXPECT_EQ(e->getName(), "Wall with Door");
    cmd.undo();
    EXPECT_EQ(e->getComponent<MeshRenderer>()->getMesh(), w.oldMesh);
    EXPECT_EQ(e->getName(), "Wall");
}

TEST(CutoutOpeningCommand, DeletedEntityMakesExecuteAndUndoDoNothing)
{
    Wall w;
    CutoutOpeningCommand cmd(w.scene, w.id, w.oldMesh, w.newMesh,
                             "Wall", "Wall with Door", "cut");
    cmd.execute();
    ASSERT_TRUE(w.scene.removeEntity(w.id));
    cmd.undo();
    cmd.execute();
    EXPECT_EQ(w.scene.findEntityById(w.id), nullptr);
}
