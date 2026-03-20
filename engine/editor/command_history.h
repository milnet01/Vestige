/// @file command_history.h
/// @brief Undo/redo stack manager with version-counter dirty tracking.
#pragma once

#include "editor/commands/editor_command.h"

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Manages the undo/redo command stack.
///
/// Commands are pushed via execute(), which calls cmd->execute() and adds it
/// to the stack. Undo/redo move a cursor through the stack. A version counter
/// tracks dirty state: isDirty() returns true when the current version differs
/// from the saved version. This correctly handles "undo back to saved state".
class CommandHistory
{
public:
    CommandHistory() = default;

    /// @brief Executes a command and pushes it onto the stack.
    /// Discards any redo branch (commands after the current index).
    /// Trims the oldest commands if the stack exceeds MAX_COMMANDS.
    void execute(std::unique_ptr<EditorCommand> cmd);

    /// @brief Undoes the last executed command.
    void undo();

    /// @brief Redoes the last undone command.
    void redo();

    /// @brief Clears the entire history (e.g., on scene load/new).
    void clear();

    /// @brief Returns true if there is a command to undo.
    bool canUndo() const;

    /// @brief Returns true if there is a command to redo.
    bool canRedo() const;

    /// @brief Returns true if the scene has changed since the last save.
    /// Uses a version counter that correctly handles undo-to-saved-state.
    bool isDirty() const;

    /// @brief Records the current version as the saved state.
    void markSaved();

    /// @brief Gets the command list (for the history panel).
    const std::vector<std::unique_ptr<EditorCommand>>& getCommands() const;

    /// @brief Gets the current index (-1 = no commands executed).
    int getCurrentIndex() const;

    /// @brief Maximum number of commands kept in history.
    static constexpr size_t MAX_COMMANDS = 200;

private:
    std::vector<std::unique_ptr<EditorCommand>> m_commands;
    int m_currentIndex = -1;
    int m_version = 0;
    int m_savedVersion = 0;
};

} // namespace Vestige
