// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file composite_command.h
/// @brief Groups multiple sub-commands into a single undo step.
#pragma once

#include "editor/commands/editor_command.h"

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Groups multiple sub-commands as a single undo/redo step.
///
/// Execute iterates forward, undo iterates in reverse. Used for multi-select
/// operations (e.g., deleting several entities at once).
class CompositeCommand : public EditorCommand
{
public:
    CompositeCommand(std::string description,
                     std::vector<std::unique_ptr<EditorCommand>> commands)
        : m_description(std::move(description))
        , m_commands(std::move(commands))
    {
    }

    void execute() override
    {
        for (auto& cmd : m_commands)
        {
            cmd->execute();
        }
    }

    void undo() override
    {
        // Reverse order — undo child before parent, etc.
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
        {
            (*it)->undo();
        }
    }

    std::string getDescription() const override
    {
        return m_description;
    }

    size_t getCommandCount() const { return m_commands.size(); }

private:
    std::string m_description;
    std::vector<std::unique_ptr<EditorCommand>> m_commands;
};

} // namespace Vestige
