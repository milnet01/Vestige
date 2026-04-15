// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file hierarchy_panel.h
/// @brief Scene hierarchy tree panel for the editor.
#pragma once

#include <cstdint>
#include <vector>

namespace Vestige
{

class CommandHistory;
class Scene;
class Entity;
class Selection;

/// @brief Draws a tree view of all entities in the active scene.
class HierarchyPanel
{
public:
    /// @brief Sets the command history for undo support.
    void setCommandHistory(CommandHistory* history);

    /// @brief Draws the hierarchy panel contents (inside an existing ImGui window).
    /// @param scene Active scene (may be nullptr).
    /// @param selection Editor selection state.
    void draw(Scene* scene, Selection& selection);

    /// @brief Returns true if the user confirmed a "Save as Prefab" action.
    bool hasPendingSavePrefab() const { return m_prefabSaveConfirmed; }

    /// @brief Gets the entity ID to save as prefab.
    uint32_t getPendingSavePrefabEntityId() const { return m_pendingSavePrefabId; }

    /// @brief Gets the user-entered prefab name.
    const char* getPendingSavePrefabName() const { return m_prefabNameBuffer; }

    /// @brief Clears the pending save-as-prefab state (called after processing).
    void clearPendingSavePrefab()
    {
        m_prefabSaveConfirmed = false;
        m_pendingSavePrefabId = 0;
    }

private:
    void drawEntityNode(Entity& entity, Scene& scene, Selection& selection);
    bool matchesFilter(const Entity& entity) const;
    void collectDescendantIds(const Entity& entity, std::vector<uint32_t>& ids) const;

    // Search filter
    char m_searchBuffer[256] = {};

    // Rename state
    uint32_t m_renamingEntityId = 0;
    char m_renameBuffer[256] = {};
    bool m_renameFocusSet = false;
    bool m_wantOpenRename = false;

    // Deferred actions (processed after tree iteration to avoid modifying during traversal)
    uint32_t m_pendingDeleteId = 0;
    bool m_pendingDeleteSelected = false;
    uint32_t m_pendingDuplicateId = 0;
    uint32_t m_pendingReparentEntityId = 0;
    uint32_t m_pendingReparentTargetId = 0;
    bool m_wantCreateEntity = false;
    uint32_t m_createParentId = 0;
    bool m_wantGroupSelected = false;
    CommandHistory* m_commandHistory = nullptr;

    // Save-as-prefab state (persists across frames while popup is open)
    uint32_t m_pendingSavePrefabId = 0;
    bool m_prefabSaveConfirmed = false;
    char m_prefabNameBuffer[256] = {};
    bool m_wantOpenPrefabSave = false;
    bool m_prefabNameFocusSet = false;
};

} // namespace Vestige
