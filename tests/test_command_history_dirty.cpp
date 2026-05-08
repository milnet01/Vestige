// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_command_history_dirty.cpp
/// @brief Phase 10.9 Slice 12 Ed7 — pin CommandHistory dirty-tracking
///        contract incl. the new `markUnsavedChange()` sticky flag.
///
/// The Ed7 fix removed `FileMenu::m_isDirty` (dual source of truth)
/// and routed the "scene wholesale-replaced" signal through a new
/// `CommandHistory::markUnsavedChange()` that flips the sticky
/// `m_savedVersionLost` flag. Tests here cover:
///   - markUnsavedChange flips isDirty true even with no commands.
///   - The flag clears on markSaved (Save / Save As path).
///   - It survives undo+redo of an unrelated command (not erased by
///     normal version arithmetic).

#include <gtest/gtest.h>
#include "editor/command_history.h"
#include "editor/commands/editor_command.h"

#include <memory>
#include <string>

namespace Vestige::CommandHistoryDirty::Test
{

namespace
{
class IncrementCommand : public EditorCommand
{
public:
    explicit IncrementCommand(int& value) : m_value(value) {}
    void execute() override { ++m_value; }
    void undo()    override { --m_value; }
    std::string getDescription() const override { return "Increment"; }
private:
    int& m_value;
};
}  // namespace

// On a fresh history, markUnsavedChange flips isDirty true.
TEST(CommandHistoryDirty, MarkUnsavedChangeFromCleanFlipsDirty_Ed7)
{
    CommandHistory h;
    EXPECT_FALSE(h.isDirty());
    h.markUnsavedChange();
    EXPECT_TRUE(h.isDirty());
}

// markSaved clears the sticky flag (it's the Save / Save As path).
TEST(CommandHistoryDirty, MarkSavedClearsStickyDirty_Ed7)
{
    CommandHistory h;
    h.markUnsavedChange();
    ASSERT_TRUE(h.isDirty());
    h.markSaved();
    EXPECT_FALSE(h.isDirty());
}

// The sticky flag isn't erased by ordinary version arithmetic — it
// survives an undo+redo of an unrelated command.
TEST(CommandHistoryDirty, StickyFlagSurvivesUndoRedo_Ed7)
{
    CommandHistory h;
    int v = 0;

    // First push a command + save baseline.
    h.execute(std::make_unique<IncrementCommand>(v));
    h.markSaved();
    ASSERT_FALSE(h.isDirty());

    // Now scene is wholesale-replaced (e.g. wizard apply).
    h.markUnsavedChange();
    ASSERT_TRUE(h.isDirty());

    // User undoes + redoes the previous command — version arithmetic
    // would put us back at saved version, but the sticky flag still
    // signals "scene differs from disk".
    h.undo();
    EXPECT_TRUE(h.isDirty());
    h.redo();
    EXPECT_TRUE(h.isDirty());
}

// Clear() resets the sticky flag too — clearing means "fresh history,
// no dirty signal needed unless the caller explicitly markUnsavedChange's".
TEST(CommandHistoryDirty, ClearResetsStickyFlag_Ed7)
{
    CommandHistory h;
    h.markUnsavedChange();
    ASSERT_TRUE(h.isDirty());
    h.clear();
    EXPECT_FALSE(h.isDirty());
}

// Pre-Ed7 baseline: pushing a command and undoing it returns isDirty
// to false (existing behaviour pin).
TEST(CommandHistoryDirty, UndoToSavedClearsDirty_Baseline)
{
    CommandHistory h;
    int v = 0;
    h.execute(std::make_unique<IncrementCommand>(v));
    EXPECT_TRUE(h.isDirty());
    h.undo();
    EXPECT_FALSE(h.isDirty());
}

}  // namespace Vestige::CommandHistoryDirty::Test
