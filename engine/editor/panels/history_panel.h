/// @file history_panel.h
/// @brief Undo history panel — scrollable list of commands with click-to-jump.
#pragma once

namespace Vestige
{

class CommandHistory;

/// @brief Draws the undo/redo history as a scrollable list.
///
/// The current position is highlighted, redo territory is grayed out.
/// Clicking an entry jumps to that point in history by undoing/redoing
/// the appropriate commands.
class HistoryPanel
{
public:
    /// @brief Draws the history panel contents inside the current ImGui window.
    /// @param history Command history to display.
    void draw(CommandHistory& history);
};

} // namespace Vestige
