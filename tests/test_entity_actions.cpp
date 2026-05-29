// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_entity_actions.cpp
/// @brief Unit tests for EntityActions utilities (multi-delete root canonicalisation).
#include "editor/entity_actions.h"
#include "editor/commands/composite_command.h"
#include "editor/commands/delete_entity_command.h"
#include "editor/command_history.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <gtest/gtest.h>

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
