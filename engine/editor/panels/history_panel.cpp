// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file history_panel.cpp
/// @brief History panel implementation — scrollable undo/redo list.
#include "editor/panels/history_panel.h"
#include "editor/command_history.h"

#include <imgui.h>

#include <string>

namespace Vestige
{

void HistoryPanel::draw(CommandHistory& history)
{
    const auto& commands = history.getCommands();
    int currentIndex = history.getCurrentIndex();

    ImGui::Text("History (%d)", static_cast<int>(commands.size()));
    ImGui::Separator();

    // "Initial State" entry — represents the state before any commands
    {
        bool isBase = (currentIndex == -1);
        if (ImGui::Selectable("Initial State", isBase))
        {
            // Undo everything to return to base state
            while (history.canUndo())
            {
                history.undo();
            }
        }
    }

    // Command entries
    for (int i = 0; i < static_cast<int>(commands.size()); ++i)
    {
        bool isCurrent = (i == currentIndex);
        bool isRedoTerritory = (i > currentIndex);

        // Gray out redo territory
        if (isRedoTerritory)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
        }

        // Build label: "N. Description"
        std::string label = std::to_string(i + 1) + ". "
            + commands[i]->getDescription();

        if (ImGui::Selectable(label.c_str(), isCurrent))
        {
            // Click-to-jump: undo or redo to reach this index
            while (history.getCurrentIndex() > i)
            {
                history.undo();
            }
            while (history.getCurrentIndex() < i)
            {
                history.redo();
            }
        }

        if (isRedoTerritory)
        {
            ImGui::PopStyleColor();
        }
    }

    // Auto-scroll to current position when it changes
    if (currentIndex >= 0)
    {
        float itemHeight = ImGui::GetTextLineHeightWithSpacing();
        float targetY = static_cast<float>(currentIndex + 1) * itemHeight;
        float scrollY = ImGui::GetScrollY();
        float windowHeight = ImGui::GetWindowHeight();

        if (targetY < scrollY || targetY > scrollY + windowHeight - itemHeight * 2)
        {
            ImGui::SetScrollY(targetY - windowHeight * 0.5f);
        }
    }
}

} // namespace Vestige
