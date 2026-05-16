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

// Slice 18 Ts3: the basic clean→dirty and markSaved-clears-dirty
// contracts are already pinned in `test_command_history.cpp`
// (`DirtyAfterExecute`, `MarkSavedClearsDirty`). The `_Ed7`-suffixed
// duplicates were trimmed here; the file now focuses on the
// sticky-flag behaviour that's genuinely new to Ed7.

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

// Slice 18 Ts3: dropped `UndoToSavedClearsDirty_Baseline` — the
// version-arithmetic-undo-clears-dirty contract is canonical in
// `test_command_history.cpp::UndoBackToSavedIsClean`.

// Audit follow-up (2026-05-16): the sticky-flag rule must override the
// version-arithmetic-undo path. After markSaved → execute → markUnsaved
// (a wizard/template apply that re-replaces the scene non-undoably),
// undoing the prior command still returns the version counter to the
// saved-version checkpoint — but the sticky flag must keep isDirty()
// true because the non-undoable replacement happened. A naive AND of
// the two conditions would fail this case.
TEST(CommandHistoryDirty, StickyFlagOverridesVersionArithmeticUndo_Ed7)
{
    CommandHistory h;
    int v = 0;
    h.execute(std::make_unique<IncrementCommand>(v));
    h.markSaved();
    EXPECT_FALSE(h.isDirty());

    h.execute(std::make_unique<IncrementCommand>(v));
    EXPECT_TRUE(h.isDirty());

    // Non-undoable replacement happens here (wizard apply / template
    // load). The sticky flag latches.
    h.markUnsavedChange();
    EXPECT_TRUE(h.isDirty());

    // Plain version-counter arithmetic would clear isDirty here — the
    // undo brings the counter back to the saved version. Sticky must win.
    h.undo();
    EXPECT_TRUE(h.isDirty())
        << "sticky flag must survive a version-arithmetic clear";

    // markSaved() is the only thing that clears the sticky flag.
    h.markSaved();
    EXPECT_FALSE(h.isDirty());
}

}  // namespace Vestige::CommandHistoryDirty::Test
