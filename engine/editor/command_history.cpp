/// @file command_history.cpp
/// @brief CommandHistory implementation — undo/redo stack with dirty tracking.
#include "editor/command_history.h"
#include "core/logger.h"

namespace Vestige
{

void CommandHistory::execute(std::unique_ptr<EditorCommand> cmd)
{
    if (!cmd)
    {
        return;
    }

    // Execute the command
    cmd->execute();

    // Discard any redo branch (commands after current index)
    if (m_currentIndex + 1 < static_cast<int>(m_commands.size()))
    {
        m_commands.erase(m_commands.begin() + m_currentIndex + 1, m_commands.end());
    }

    // Push the new command
    m_commands.push_back(std::move(cmd));
    m_currentIndex = static_cast<int>(m_commands.size()) - 1;
    ++m_version;

    // Trim oldest commands if we exceed the limit
    if (m_commands.size() > MAX_COMMANDS)
    {
        size_t trimCount = m_commands.size() - MAX_COMMANDS;
        m_commands.erase(m_commands.begin(),
                         m_commands.begin() + static_cast<ptrdiff_t>(trimCount));
        m_currentIndex -= static_cast<int>(trimCount);

        // If the saved version was in the trimmed region, it's permanently lost
        m_savedVersion -= static_cast<int>(trimCount);
        // A negative savedVersion that we can never reach again means permanently dirty
    }

    Logger::info("Executed: " + m_commands[static_cast<size_t>(m_currentIndex)]->getDescription());
}

void CommandHistory::undo()
{
    if (!canUndo())
    {
        return;
    }

    m_commands[static_cast<size_t>(m_currentIndex)]->undo();
    Logger::info("Undo: " + m_commands[static_cast<size_t>(m_currentIndex)]->getDescription());
    --m_currentIndex;
    --m_version;
}

void CommandHistory::redo()
{
    if (!canRedo())
    {
        return;
    }

    ++m_currentIndex;
    ++m_version;
    m_commands[static_cast<size_t>(m_currentIndex)]->execute();
    Logger::info("Redo: " + m_commands[static_cast<size_t>(m_currentIndex)]->getDescription());
}

void CommandHistory::clear()
{
    m_commands.clear();
    m_currentIndex = -1;
    m_version = 0;
    m_savedVersion = 0;
}

bool CommandHistory::canUndo() const
{
    return m_currentIndex >= 0;
}

bool CommandHistory::canRedo() const
{
    return m_currentIndex + 1 < static_cast<int>(m_commands.size());
}

bool CommandHistory::isDirty() const
{
    return m_version != m_savedVersion;
}

void CommandHistory::markSaved()
{
    m_savedVersion = m_version;
}

const std::vector<std::unique_ptr<EditorCommand>>& CommandHistory::getCommands() const
{
    return m_commands;
}

int CommandHistory::getCurrentIndex() const
{
    return m_currentIndex;
}

} // namespace Vestige
