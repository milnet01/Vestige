/// @file hierarchy_panel.h
/// @brief Scene hierarchy tree panel for the editor.
#pragma once

#include <cstdint>
#include <vector>

namespace Vestige
{

class Scene;
class Entity;
class Selection;

/// @brief Draws a tree view of all entities in the active scene.
class HierarchyPanel
{
public:
    /// @brief Draws the hierarchy panel contents (inside an existing ImGui window).
    /// @param scene Active scene (may be nullptr).
    /// @param selection Editor selection state.
    void draw(Scene* scene, Selection& selection);

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
    uint32_t m_pendingReparentEntityId = 0;
    uint32_t m_pendingReparentTargetId = 0;
    bool m_wantCreateEntity = false;
    uint32_t m_createParentId = 0;
};

} // namespace Vestige
