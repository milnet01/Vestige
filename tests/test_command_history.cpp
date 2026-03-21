/// @file test_command_history.cpp
/// @brief Unit tests for CommandHistory undo/redo and dirty tracking.
#include "editor/command_history.h"
#include "editor/commands/editor_command.h"
#include "editor/commands/transform_command.h"
#include "editor/commands/composite_command.h"
#include "editor/commands/create_entity_command.h"
#include "editor/commands/delete_entity_command.h"
#include "editor/commands/entity_property_command.h"
#include "editor/commands/reparent_command.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Simple test command — increments/decrements an integer
// ---------------------------------------------------------------------------

class IncrementCommand : public EditorCommand
{
public:
    explicit IncrementCommand(int& value) : m_value(value), m_delta(1) {}

    void execute() override { m_value += m_delta; }
    void undo() override { m_value -= m_delta; }
    std::string getDescription() const override { return "Increment"; }

private:
    int& m_value;
    int m_delta;
};

// ---------------------------------------------------------------------------
// CommandHistory basic tests
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, StartsEmpty)
{
    CommandHistory history;
    EXPECT_FALSE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.getCurrentIndex(), -1);
    EXPECT_FALSE(history.isDirty());
}

TEST(CommandHistoryTest, ExecuteCommand)
{
    int value = 0;
    CommandHistory history;

    history.execute(std::make_unique<IncrementCommand>(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.getCurrentIndex(), 0);
}

TEST(CommandHistoryTest, UndoRestoresState)
{
    int value = 0;
    CommandHistory history;

    history.execute(std::make_unique<IncrementCommand>(value));
    EXPECT_EQ(value, 1);

    history.undo();
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(history.canUndo());
    EXPECT_TRUE(history.canRedo());
}

TEST(CommandHistoryTest, RedoReappliesState)
{
    int value = 0;
    CommandHistory history;

    history.execute(std::make_unique<IncrementCommand>(value));
    history.undo();
    EXPECT_EQ(value, 0);

    history.redo();
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
}

TEST(CommandHistoryTest, NewCommandDiscardsRedoBranch)
{
    int value = 0;
    CommandHistory history;

    history.execute(std::make_unique<IncrementCommand>(value));
    history.execute(std::make_unique<IncrementCommand>(value));
    history.execute(std::make_unique<IncrementCommand>(value));
    EXPECT_EQ(value, 3);

    history.undo();
    history.undo();
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(history.canRedo());

    // New command discards the 2 undone commands
    history.execute(std::make_unique<IncrementCommand>(value));
    EXPECT_EQ(value, 2);
    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.getCommands().size(), 2u);
}

TEST(CommandHistoryTest, ClearResetsEverything)
{
    int value = 0;
    CommandHistory history;

    history.execute(std::make_unique<IncrementCommand>(value));
    history.execute(std::make_unique<IncrementCommand>(value));

    history.clear();
    EXPECT_FALSE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
    EXPECT_EQ(history.getCurrentIndex(), -1);
    EXPECT_FALSE(history.isDirty());
    EXPECT_EQ(history.getCommands().size(), 0u);
}

// ---------------------------------------------------------------------------
// Dirty tracking (version counter)
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, DirtyAfterExecute)
{
    int value = 0;
    CommandHistory history;

    EXPECT_FALSE(history.isDirty());
    history.execute(std::make_unique<IncrementCommand>(value));
    EXPECT_TRUE(history.isDirty());
}

TEST(CommandHistoryTest, MarkSavedClearsDirty)
{
    int value = 0;
    CommandHistory history;

    history.execute(std::make_unique<IncrementCommand>(value));
    EXPECT_TRUE(history.isDirty());

    history.markSaved();
    EXPECT_FALSE(history.isDirty());
}

TEST(CommandHistoryTest, UndoBackToSavedIsClean)
{
    int value = 0;
    CommandHistory history;

    history.execute(std::make_unique<IncrementCommand>(value));
    history.markSaved();
    EXPECT_FALSE(history.isDirty());

    history.execute(std::make_unique<IncrementCommand>(value));
    EXPECT_TRUE(history.isDirty());

    history.undo();
    EXPECT_FALSE(history.isDirty());  // Back at saved version
}

TEST(CommandHistoryTest, DirtyAfterUndoPastSaved)
{
    int value = 0;
    CommandHistory history;

    history.execute(std::make_unique<IncrementCommand>(value));
    history.markSaved();

    history.undo();
    EXPECT_TRUE(history.isDirty());  // Before saved version
}

// ---------------------------------------------------------------------------
// Command limit
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, MaxCommandsTrimming)
{
    int value = 0;
    CommandHistory history;

    // Execute MAX_COMMANDS + 10 commands
    for (size_t i = 0; i < CommandHistory::MAX_COMMANDS + 10; ++i)
    {
        history.execute(std::make_unique<IncrementCommand>(value));
    }

    EXPECT_EQ(history.getCommands().size(), CommandHistory::MAX_COMMANDS);
    EXPECT_EQ(static_cast<size_t>(value), CommandHistory::MAX_COMMANDS + 10);
}

// ---------------------------------------------------------------------------
// CompositeCommand
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, CompositeCommandUndoesInReverse)
{
    // Track execution order
    std::vector<int> log;

    class LogCommand : public EditorCommand
    {
    public:
        LogCommand(std::vector<int>& log, int id) : m_log(log), m_id(id) {}
        void execute() override { m_log.push_back(m_id); }
        void undo() override { m_log.push_back(-m_id); }
        std::string getDescription() const override { return "Log " + std::to_string(m_id); }
    private:
        std::vector<int>& m_log;
        int m_id;
    };

    std::vector<std::unique_ptr<EditorCommand>> cmds;
    cmds.push_back(std::make_unique<LogCommand>(log, 1));
    cmds.push_back(std::make_unique<LogCommand>(log, 2));
    cmds.push_back(std::make_unique<LogCommand>(log, 3));

    CommandHistory history;
    history.execute(std::make_unique<CompositeCommand>("Test composite", std::move(cmds)));

    // Execute order: 1, 2, 3
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 1);
    EXPECT_EQ(log[1], 2);
    EXPECT_EQ(log[2], 3);

    log.clear();
    history.undo();

    // Undo order: -3, -2, -1 (reverse)
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], -3);
    EXPECT_EQ(log[1], -2);
    EXPECT_EQ(log[2], -1);
}

// ---------------------------------------------------------------------------
// TransformCommand with real entities
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, TransformCommandUndoRedo)
{
    Scene scene("Test");
    Entity* entity = scene.createEntity("Box");
    entity->transform.position = glm::vec3(1.0f, 2.0f, 3.0f);
    entity->transform.rotation = glm::vec3(0.0f);
    entity->transform.scale = glm::vec3(1.0f);

    uint32_t id = entity->getId();

    CommandHistory history;

    auto cmd = std::make_unique<TransformCommand>(
        scene, id,
        glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.0f), glm::vec3(1.0f),  // old
        glm::vec3(5.0f, 6.0f, 7.0f), glm::vec3(90.0f, 0.0f, 0.0f), glm::vec3(2.0f)  // new
    );
    history.execute(std::move(cmd));

    // After execute: new transform
    EXPECT_FLOAT_EQ(entity->transform.position.x, 5.0f);
    EXPECT_FLOAT_EQ(entity->transform.rotation.x, 90.0f);
    EXPECT_FLOAT_EQ(entity->transform.scale.x, 2.0f);

    // Undo: old transform
    history.undo();
    EXPECT_FLOAT_EQ(entity->transform.position.x, 1.0f);
    EXPECT_FLOAT_EQ(entity->transform.rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(entity->transform.scale.x, 1.0f);

    // Redo: new transform
    history.redo();
    EXPECT_FLOAT_EQ(entity->transform.position.x, 5.0f);
}

// ---------------------------------------------------------------------------
// CreateEntityCommand
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, CreateEntityCommandUndoRemovesEntity)
{
    Scene scene("Test");
    Entity* entity = scene.createEntity("Cube");
    uint32_t id = entity->getId();
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 1u);

    CommandHistory history;
    history.execute(std::make_unique<CreateEntityCommand>(scene, id));

    // Entity still exists after execute (it was already created)
    EXPECT_NE(scene.findEntityById(id), nullptr);
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 1u);

    // Undo removes it
    history.undo();
    EXPECT_EQ(scene.findEntityById(id), nullptr);
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 0u);

    // Redo re-inserts it
    history.redo();
    EXPECT_NE(scene.findEntityById(id), nullptr);
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 1u);
}

// ---------------------------------------------------------------------------
// DeleteEntityCommand
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, DeleteEntityCommandUndoRestoresEntity)
{
    Scene scene("Test");
    Entity* entity = scene.createEntity("Sphere");
    entity->transform.position = glm::vec3(10.0f, 20.0f, 30.0f);
    uint32_t id = entity->getId();
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 1u);

    CommandHistory history;
    history.execute(std::make_unique<DeleteEntityCommand>(scene, id));

    // Entity is gone
    EXPECT_EQ(scene.findEntityById(id), nullptr);
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 0u);

    // Undo restores it (same ID, same transform)
    history.undo();
    Entity* restored = scene.findEntityById(id);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->getName(), "Sphere");
    EXPECT_FLOAT_EQ(restored->transform.position.x, 10.0f);
    EXPECT_FLOAT_EQ(restored->transform.position.y, 20.0f);
    EXPECT_FLOAT_EQ(restored->transform.position.z, 30.0f);
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 1u);
}

TEST(CommandHistoryTest, DeleteEntityPreservesSiblingOrder)
{
    Scene scene("Test");
    scene.createEntity("A");
    Entity* b = scene.createEntity("B");
    scene.createEntity("C");

    uint32_t bId = b->getId();
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 3u);

    CommandHistory history;
    history.execute(std::make_unique<DeleteEntityCommand>(scene, bId));
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 2u);

    // Undo — B should be back at index 1 (between A and C)
    history.undo();
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 3u);

    const auto& children = scene.getRoot()->getChildren();
    EXPECT_EQ(children[0]->getName(), "A");
    EXPECT_EQ(children[1]->getName(), "B");
    EXPECT_EQ(children[2]->getName(), "C");
}

// ---------------------------------------------------------------------------
// Entity insertChild
// ---------------------------------------------------------------------------

TEST(EntityTest, InsertChildAtIndex)
{
    Scene scene("Test");
    scene.createEntity("A");
    scene.createEntity("B");
    scene.createEntity("C");

    auto newChild = std::make_unique<Entity>("X");
    scene.getRoot()->insertChild(std::move(newChild), 1);

    const auto& children = scene.getRoot()->getChildren();
    ASSERT_EQ(children.size(), 4u);
    EXPECT_EQ(children[0]->getName(), "A");
    EXPECT_EQ(children[1]->getName(), "X");
    EXPECT_EQ(children[2]->getName(), "B");
    EXPECT_EQ(children[3]->getName(), "C");
}

TEST(EntityTest, InsertChildAtEnd)
{
    Scene scene("Test");
    scene.createEntity("A");

    auto newChild = std::make_unique<Entity>("Z");
    scene.getRoot()->insertChild(std::move(newChild), 999);  // Beyond size

    const auto& children = scene.getRoot()->getChildren();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0]->getName(), "A");
    EXPECT_EQ(children[1]->getName(), "Z");
}

// ---------------------------------------------------------------------------
// EntityPropertyCommand — name
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, EntityPropertyCommandNameUndoRedo)
{
    Scene scene("Test");
    Entity* entity = scene.createEntity("OriginalName");
    uint32_t id = entity->getId();

    CommandHistory history;
    history.execute(std::make_unique<EntityPropertyCommand>(
        scene, id, EntityProperty::NAME,
        std::string("OriginalName"), std::string("NewName")));

    EXPECT_EQ(entity->getName(), "NewName");

    history.undo();
    EXPECT_EQ(entity->getName(), "OriginalName");

    history.redo();
    EXPECT_EQ(entity->getName(), "NewName");
}

// ---------------------------------------------------------------------------
// EntityPropertyCommand — visible
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, EntityPropertyCommandVisibleUndoRedo)
{
    Scene scene("Test");
    Entity* entity = scene.createEntity("Box");
    uint32_t id = entity->getId();

    EXPECT_TRUE(entity->isVisible());

    CommandHistory history;
    history.execute(std::make_unique<EntityPropertyCommand>(
        scene, id, EntityProperty::VISIBLE, true, false));

    EXPECT_FALSE(entity->isVisible());

    history.undo();
    EXPECT_TRUE(entity->isVisible());

    history.redo();
    EXPECT_FALSE(entity->isVisible());
}

// ---------------------------------------------------------------------------
// EntityPropertyCommand — locked
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, EntityPropertyCommandLockedUndoRedo)
{
    Scene scene("Test");
    Entity* entity = scene.createEntity("Box");
    uint32_t id = entity->getId();

    EXPECT_FALSE(entity->isLocked());

    CommandHistory history;
    history.execute(std::make_unique<EntityPropertyCommand>(
        scene, id, EntityProperty::LOCKED, false, true));

    EXPECT_TRUE(entity->isLocked());

    history.undo();
    EXPECT_FALSE(entity->isLocked());
}

// ---------------------------------------------------------------------------
// EntityPropertyCommand — active
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, EntityPropertyCommandActiveUndoRedo)
{
    Scene scene("Test");
    Entity* entity = scene.createEntity("Box");
    uint32_t id = entity->getId();

    EXPECT_TRUE(entity->isActive());

    CommandHistory history;
    history.execute(std::make_unique<EntityPropertyCommand>(
        scene, id, EntityProperty::ACTIVE, true, false));

    EXPECT_FALSE(entity->isActive());

    history.undo();
    EXPECT_TRUE(entity->isActive());
}

// ---------------------------------------------------------------------------
// ReparentCommand
// ---------------------------------------------------------------------------

TEST(CommandHistoryTest, ReparentCommandUndoRedo)
{
    Scene scene("Test");
    Entity* a = scene.createEntity("A");
    Entity* b = scene.createEntity("B");
    uint32_t aId = a->getId();
    uint32_t bId = b->getId();

    EXPECT_EQ(scene.getRoot()->getChildren().size(), 2u);
    EXPECT_EQ(a->getParent(), scene.getRoot());
    EXPECT_EQ(b->getParent(), scene.getRoot());

    // Reparent B under A
    CommandHistory history;
    history.execute(std::make_unique<ReparentCommand>(scene, bId, aId));

    EXPECT_EQ(scene.getRoot()->getChildren().size(), 1u);
    EXPECT_EQ(b->getParent(), a);
    EXPECT_EQ(a->getChildren().size(), 1u);

    // Undo: B should be back at root
    history.undo();
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 2u);
    EXPECT_EQ(b->getParent(), scene.getRoot());
    EXPECT_EQ(a->getChildren().size(), 0u);

    // Redo: B under A again
    history.redo();
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 1u);
    EXPECT_EQ(b->getParent(), a);
}

TEST(CommandHistoryTest, ReparentCommandPreservesSiblingOrder)
{
    Scene scene("Test");
    scene.createEntity("A");
    Entity* b = scene.createEntity("B");
    scene.createEntity("C");
    Entity* d = scene.createEntity("D");

    uint32_t bId = b->getId();
    uint32_t dId = d->getId();

    // Reparent B under D
    CommandHistory history;
    history.execute(std::make_unique<ReparentCommand>(scene, bId, dId));

    EXPECT_EQ(scene.getRoot()->getChildren().size(), 3u);
    EXPECT_EQ(scene.getRoot()->getChildren()[0]->getName(), "A");
    EXPECT_EQ(scene.getRoot()->getChildren()[1]->getName(), "C");
    EXPECT_EQ(scene.getRoot()->getChildren()[2]->getName(), "D");

    // Undo: B should be back at index 1 (between A and C)
    history.undo();
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 4u);
    EXPECT_EQ(scene.getRoot()->getChildren()[0]->getName(), "A");
    EXPECT_EQ(scene.getRoot()->getChildren()[1]->getName(), "B");
    EXPECT_EQ(scene.getRoot()->getChildren()[2]->getName(), "C");
    EXPECT_EQ(scene.getRoot()->getChildren()[3]->getName(), "D");
}
