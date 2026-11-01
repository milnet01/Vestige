// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_entity_actions.cpp
/// @brief Unit tests for EntityActions utilities (multi-delete root canonicalisation).
#include "editor/entity_actions.h"
#include "editor/commands/composite_command.h"
#include "editor/commands/delete_entity_command.h"
#include "editor/command_history.h"
#include "editor/commands/group_entities_command.h"
#include "editor/selection.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace Vestige;

// Phase 10.9 Slice 12 Ed3 — multi-delete root canonicalisation
// ---------------------------------------------------------------------------
// Selecting a parent + descendant and pressing Delete must drop the
// descendant from the operation list — its parent's recursive removal
// already wipes it, and a second DeleteEntityCommand on a freed id fails
// undo silently. `EntityActions::filterToRootEntities` and
// `EntityActions::buildDeleteCommand` close the gap.

TEST(EntityActionsFilterRoots, KeepsDisjointSiblings_Ed3)
{
    Scene scene("Test");
    Entity* a = scene.createEntity("A");
    Entity* b = scene.createEntity("B");
    Entity* c = scene.createEntity("C");

    auto roots = EntityActions::filterToRootEntities(scene,
        {a->getId(), b->getId(), c->getId()});

    ASSERT_EQ(roots.size(), 3u);
    EXPECT_EQ(roots[0], a->getId());
    EXPECT_EQ(roots[1], b->getId());
    EXPECT_EQ(roots[2], c->getId());
}

TEST(EntityActionsFilterRoots, DropsDescendantOfSelectedParent_Ed3)
{
    Scene scene("Test");
    Entity* parent = scene.createEntity("Parent");
    Entity* child = scene.createEntity("Child");
    Entity* grandchild = scene.createEntity("Grandchild");
    ASSERT_TRUE(scene.reparentEntity(child->getId(), parent->getId()));
    ASSERT_TRUE(scene.reparentEntity(grandchild->getId(), child->getId()));

    // Selecting Parent + Child + Grandchild should leave just Parent.
    auto roots = EntityActions::filterToRootEntities(scene,
        {parent->getId(), child->getId(), grandchild->getId()});

    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], parent->getId());
}

TEST(EntityActionsFilterRoots, KeepsChildWhenParentNotSelected_Ed3)
{
    Scene scene("Test");
    Entity* parent = scene.createEntity("Parent");
    Entity* child = scene.createEntity("Child");
    ASSERT_TRUE(scene.reparentEntity(child->getId(), parent->getId()));

    // Only the child is selected — parent stays put. Child is its own root
    // for this operation.
    auto roots = EntityActions::filterToRootEntities(scene, {child->getId()});

    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], child->getId());
}

TEST(EntityActionsFilterRoots, MixedTreeKeepsSiblingAndPartialBranch_Ed3)
{
    Scene scene("Test");
    Entity* a = scene.createEntity("A");
    Entity* aChild = scene.createEntity("AChild");
    Entity* b = scene.createEntity("B");
    Entity* bChild = scene.createEntity("BChild");
    ASSERT_TRUE(scene.reparentEntity(aChild->getId(), a->getId()));
    ASSERT_TRUE(scene.reparentEntity(bChild->getId(), b->getId()));

    // Select A (parent) + AChild (descendant of A — drop) + BChild (B not
    // selected — keep). Result: {A, BChild}.
    auto roots = EntityActions::filterToRootEntities(scene,
        {a->getId(), aChild->getId(), bChild->getId()});

    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0], a->getId());
    EXPECT_EQ(roots[1], bChild->getId());
}

TEST(EntityActionsBuildDeleteCommand, EmptySelectionReturnsNullptr_Ed3)
{
    Scene scene("Test");
    auto cmd = EntityActions::buildDeleteCommand(scene, {});
    EXPECT_EQ(cmd, nullptr);
}

TEST(EntityActionsBuildDeleteCommand, ReturnsBareCommandForOneRoot_Ed3)
{
    // Even when the selection has multiple ids, if filter collapses to one
    // root (parent + descendants) the result should be a bare
    // DeleteEntityCommand, not a Composite of size 1. Description format
    // is `DeleteEntityCommand`'s own ("Delete '<name>'"), not Composite's.
    Scene scene("Test");
    Entity* parent = scene.createEntity("P");
    Entity* child = scene.createEntity("C");
    ASSERT_TRUE(scene.reparentEntity(child->getId(), parent->getId()));

    auto cmd = EntityActions::buildDeleteCommand(scene,
        {parent->getId(), child->getId()});
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->getDescription(), "Delete 'P'");
}

TEST(EntityActionsBuildDeleteCommand, MultiDeleteUndoRestoresFullTree_Ed3)
{
    // The headline case the bug bites: parent + child both selected, the
    // pre-Ed3 path would issue two DeleteEntityCommands and the second
    // would target a freed id; undo couldn't restore the original parent-
    // child topology. With Ed3, only the parent is deleted, undo brings
    // back the whole subtree.
    Scene scene("Test");
    Entity* parent = scene.createEntity("Parent");
    Entity* child = scene.createEntity("Child");
    ASSERT_TRUE(scene.reparentEntity(child->getId(), parent->getId()));
    uint32_t pId = parent->getId();
    uint32_t cId = child->getId();

    CommandHistory history;
    auto cmd = EntityActions::buildDeleteCommand(scene, {pId, cId});
    ASSERT_NE(cmd, nullptr);
    history.execute(std::move(cmd));

    EXPECT_EQ(scene.findEntityById(pId), nullptr);
    EXPECT_EQ(scene.findEntityById(cId), nullptr);

    history.undo();
    Entity* restoredParent = scene.findEntityById(pId);
    Entity* restoredChild = scene.findEntityById(cId);
    ASSERT_NE(restoredParent, nullptr);
    ASSERT_NE(restoredChild, nullptr);
    // Topology preserved — Child is back under Parent, not at scene root.
    EXPECT_EQ(restoredChild->getParent(), restoredParent);
    EXPECT_EQ(restoredParent->getChildren().size(), 1u);
}

TEST(EntityActionsBuildDeleteCommand, TwoDisjointTreesProduceComposite_Ed3)
{
    Scene scene("Test");
    Entity* a = scene.createEntity("A");
    Entity* b = scene.createEntity("B");

    auto cmd = EntityActions::buildDeleteCommand(scene, {a->getId(), b->getId()});
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->getDescription(), "Delete 2 entities");
}

TEST(EntityActionsNaming, HugeSuffixOnSiblingDoesNotThrow)
{
    Scene scene("naming");
    Entity* parent = scene.createEntity("Parent");
    scene.createEntity("Cube");
    Entity* sibling = scene.createEntity("Cube (99999999999)");
    scene.reparentEntity(sibling->getId(), parent->getId());

    std::string name;
    EXPECT_NO_THROW(name = EntityActions::generateDuplicateName("Cube", parent));
    EXPECT_FALSE(name.empty());
}

// ---------------------------------------------------------------------------
// 3D_E-0855 — grouping keeps the whole world transform
// ---------------------------------------------------------------------------

namespace
{

/// A rotated, scaled parent with one child under it, plus a plain sibling.
struct GroupFixture
{
    Scene scene{"Group"};
    Selection selection;
    Entity* child = nullptr;
    Entity* other = nullptr;

    GroupFixture()
    {
        Entity* parent = scene.createEntity("Parent");
        parent->transform.position = glm::vec3(5.0f, 1.0f, -2.0f);
        parent->transform.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
        parent->transform.scale = glm::vec3(2.0f, 2.0f, 2.0f);
        child = parent->addChild(std::make_unique<Entity>("Child"));
        scene.registerEntityRecursive(child);
        child->transform.position = glm::vec3(1.0f, 0.0f, 0.0f);
        child->transform.rotation = glm::vec3(0.0f, 30.0f, 0.0f);
        child->transform.scale = glm::vec3(1.0f, 3.0f, 1.0f);
        other = scene.createEntity("Other");
        other->transform.position = glm::vec3(-4.0f, 0.0f, 6.0f);
        scene.update(0.0f);
    }
};

void expectMatrixNear(const glm::mat4& a, const glm::mat4& b)
{
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            EXPECT_NEAR(a[c][r], b[c][r], 1e-4f) << "column " << c << " row " << r;
        }
    }
}

}  // namespace

TEST(EntityActionsGroup, CommandKeepsWorldOrientationAndSize_0855)
{
    GroupFixture f;
    const glm::mat4 before = f.child->getWorldMatrix();

    GroupEntitiesCommand cmd(f.scene, f.selection,
                             {f.child->getId(), f.other->getId()});
    cmd.execute();
    f.scene.update(0.0f);

    expectMatrixNear(f.child->getWorldMatrix(), before);
}

TEST(EntityActionsGroup, UndoRestoresLocalTransformExactly_0855)
{
    GroupFixture f;
    const glm::vec3 pos = f.child->transform.position;
    const glm::vec3 rot = f.child->transform.rotation;
    const glm::vec3 scl = f.child->transform.scale;
    const Entity* oldParent = f.child->getParent();

    GroupEntitiesCommand cmd(f.scene, f.selection,
                             {f.child->getId(), f.other->getId()});
    cmd.execute();
    cmd.undo();

    EXPECT_EQ(f.child->getParent(), oldParent);
    EXPECT_EQ(f.child->transform.position, pos);
    EXPECT_EQ(f.child->transform.rotation, rot);
    EXPECT_EQ(f.child->transform.scale, scl);

    // Redo keeps the world transform too.
    const glm::mat4 before = f.child->getWorldMatrix();
    cmd.execute();
    f.scene.update(0.0f);
    expectMatrixNear(f.child->getWorldMatrix(), before);
}

TEST(EntityActionsGroup, DirectGroupKeepsWorldOrientationAndSize_0855)
{
    GroupFixture f;
    const glm::mat4 before = f.child->getWorldMatrix();
    f.selection.select(f.child->getId());
    f.selection.addToSelection(f.other->getId());

    ASSERT_NE(EntityActions::groupEntities(f.scene, f.selection), nullptr);
    f.scene.update(0.0f);

    expectMatrixNear(f.child->getWorldMatrix(), before);
}
