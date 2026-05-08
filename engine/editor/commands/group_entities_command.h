// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file group_entities_command.h
/// @brief Phase 10.9 Slice 12 Ed7 — undoable Ctrl+G entity grouping.
///
/// Pre-Ed7 the Group action called `EntityActions::groupEntities` and
/// then `m_fileMenu.markDirty()` directly, bypassing CommandHistory
/// entirely. That meant:
///   (a) Ctrl+Z couldn't undo a group.
///   (b) `markDirty()` stuck forever — even an exhaustive undo of every
///       command in history left the editor reporting unsaved changes.
///
/// This command threads the create-group + reparent-children +
/// adjust-local-positions mutation through the standard
/// execute/undo/redo path, capturing per-child old-state on first
/// execute so undo can restore exactly the pre-group hierarchy and
/// positions. The redo path uses `CreateEntityCommand`'s stash-then-
/// reinsert trick to preserve the group entity's id across an
/// undo→redo round-trip (important so ReparentCommand-style children
/// can find their parent again).

#pragma once

#include "editor/commands/editor_command.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "editor/selection.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace Vestige
{

class GroupEntitiesCommand : public EditorCommand
{
public:
    /// @brief Builds a group of @a entityIds. The selection ref is held
    ///        so execute/undo can update it (existing groupEntities API
    ///        contract: select the group on apply).
    GroupEntitiesCommand(Scene& scene, Selection& selection,
                         std::vector<uint32_t> entityIds);

    void execute() override;
    void undo()    override;
    std::string getDescription() const override;

    /// @brief ID of the created group entity (valid after first execute,
    ///        zero before). Stays valid across undo/redo because the
    ///        execute path stashes the detached entity rather than
    ///        deleting it.
    uint32_t getGroupId() const { return m_groupId; }

private:
    struct ChildState
    {
        uint32_t   id              = 0;
        uint32_t   oldParentId     = 0;       ///< 0 → scene root.
        size_t     oldSiblingIndex = 0;
        glm::vec3  oldLocalPosition{0.0f};
        glm::vec3  newLocalPosition{0.0f};
    };

    Scene&                  m_scene;
    Selection&              m_selection;
    std::vector<uint32_t>   m_entityIds;

    bool                    m_executedOnce = false;
    uint32_t                m_groupId      = 0;
    glm::vec3               m_centroid{0.0f};
    std::vector<ChildState> m_childStates;
    std::unique_ptr<Entity> m_ownedGroup;     ///< After undo: holds detached group.
    uint32_t                m_oldSelectionPrimary = 0;  ///< Selection to restore on undo.

    static size_t findSiblingIndex(const Entity* parent, const Entity* child);
};

}  // namespace Vestige
