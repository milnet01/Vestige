/// @file editor_command.h
/// @brief Abstract base class for undoable editor commands.
#pragma once

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Abstract base for all undoable editor commands.
///
/// Commands are executed via CommandHistory::execute(), which calls execute()
/// and pushes the command onto the undo stack. Undo calls undo(), redo calls
/// execute() again.
class EditorCommand
{
public:
    virtual ~EditorCommand() = default;

    /// @brief Applies the change (first time or redo).
    virtual void execute() = 0;

    /// @brief Reverses the change.
    virtual void undo() = 0;

    /// @brief Returns a human-readable description for the history panel.
    virtual std::string getDescription() const = 0;

    /// @brief Whether this command can merge with the next one (e.g., slider drags).
    virtual bool canMergeWith(const EditorCommand& /*other*/) const { return false; }

    /// @brief Merges the given command's data into this one.
    virtual void mergeWith(EditorCommand& /*other*/) {}
};

} // namespace Vestige
