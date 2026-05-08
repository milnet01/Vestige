// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file group_entities_command.cpp
/// @brief Phase 10.9 Slice 12 Ed7 — implementation.

#include "editor/commands/group_entities_command.h"
#include "core/logger.h"

namespace Vestige
{

GroupEntitiesCommand::GroupEntitiesCommand(Scene& scene, Selection& selection,
                                           std::vector<uint32_t> entityIds)
    : m_scene(scene)
    , m_selection(selection)
    , m_entityIds(std::move(entityIds))
{
}

size_t GroupEntitiesCommand::findSiblingIndex(const Entity* parent, const Entity* child)
{
    const auto& children = parent->getChildren();
    for (size_t i = 0; i < children.size(); ++i)
    {
        if (children[i].get() == child)
        {
            return i;
        }
    }
    return children.size();
}

void GroupEntitiesCommand::execute()
{
    if (!m_executedOnce)
    {
        // First execute: do the work + capture state for undo/redo.
        m_oldSelectionPrimary = m_selection.getPrimaryId();

        // Centroid + per-child old state (mirrors EntityActions::groupEntities).
        glm::vec3 centroid(0.0f);
        int count = 0;
        std::vector<glm::vec3> oldWorldPositions;

        for (uint32_t id : m_entityIds)
        {
            Entity* e = m_scene.findEntityById(id);
            if (!e || e == m_scene.getRoot()) continue;

            ChildState s;
            s.id = id;
            Entity* parent = e->getParent();
            s.oldParentId = parent ? parent->getId() : 0;
            s.oldSiblingIndex = parent ? findSiblingIndex(parent, e) : 0;
            s.oldLocalPosition = e->transform.position;

            glm::vec3 wp = e->getWorldPosition();
            centroid += wp;
            oldWorldPositions.push_back(wp);
            m_childStates.push_back(s);
            ++count;
        }

        if (count < 2)
        {
            // Nothing to do — leave m_executedOnce false so a re-execute
            // doesn't treat this as a redo of an empty op.
            m_childStates.clear();
            return;
        }

        centroid /= static_cast<float>(count);
        m_centroid = centroid;

        // Create the group at scene root with the centroid position.
        Entity* group = m_scene.createEntity("Group");
        group->transform.position = centroid;
        m_groupId = group->getId();

        // Reparent each child + adjust local position so world position is preserved.
        for (size_t i = 0; i < m_childStates.size(); ++i)
        {
            ChildState& s = m_childStates[i];
            m_scene.reparentEntity(s.id, m_groupId);
            Entity* e = m_scene.findEntityById(s.id);
            if (e)
            {
                glm::vec3 newLocal = oldWorldPositions[i] - centroid;
                e->transform.position = newLocal;
                s.newLocalPosition = newLocal;
            }
        }

        m_selection.select(m_groupId);
        m_executedOnce = true;
        Logger::info("Grouped " + std::to_string(count) + " entities");
        return;
    }

    // Redo path: re-insert the previously detached group, then reparent
    // children + restore their group-local positions.
    if (m_ownedGroup)
    {
        Entity* root = m_scene.getRoot();
        if (root)
        {
            Entity* ptr = root->addChild(std::move(m_ownedGroup));
            m_scene.registerEntityRecursive(ptr);
        }
    }

    for (const ChildState& s : m_childStates)
    {
        m_scene.reparentEntity(s.id, m_groupId);
        Entity* e = m_scene.findEntityById(s.id);
        if (e)
        {
            e->transform.position = s.newLocalPosition;
        }
    }

    m_selection.select(m_groupId);
}

void GroupEntitiesCommand::undo()
{
    if (!m_executedOnce || m_groupId == 0) return;

    // Restore each child to its original parent + sibling index + local
    // position, in reverse order so sibling indices stay valid.
    for (auto it = m_childStates.rbegin(); it != m_childStates.rend(); ++it)
    {
        const ChildState& s = *it;
        Entity* e = m_scene.findEntityById(s.id);
        if (!e) continue;

        // reparentEntity appends to the new parent's children; we
        // need the original sibling index, so do it manually here:
        // remove + insert at index.
        Entity* curParent = e->getParent();
        Entity* targetParent = (s.oldParentId == 0)
            ? m_scene.getRoot()
            : m_scene.findEntityById(s.oldParentId);
        if (!targetParent) targetParent = m_scene.getRoot();
        if (!curParent || !targetParent) continue;

        auto owned = curParent->removeChild(e);
        if (owned)
        {
            // insertChild clamps oldSiblingIndex if children resized.
            targetParent->insertChild(std::move(owned), s.oldSiblingIndex);
            Entity* refound = m_scene.findEntityById(s.id);
            if (refound)
            {
                refound->transform.position = s.oldLocalPosition;
            }
        }
    }

    // Detach the group entity from root and stash it for redo.
    Entity* group = m_scene.findEntityById(m_groupId);
    if (group)
    {
        Entity* root = m_scene.getRoot();
        if (root)
        {
            m_scene.unregisterEntityRecursive(group);
            m_ownedGroup = root->removeChild(group);
        }
    }

    // Restore the prior selection so the user sees what they had before
    // hitting Ctrl+G. clearSelection if there was none.
    if (m_oldSelectionPrimary != 0)
    {
        m_selection.select(m_oldSelectionPrimary);
    }
    else
    {
        m_selection.clearSelection();
    }
}

std::string GroupEntitiesCommand::getDescription() const
{
    return "Group " + std::to_string(m_childStates.size()) + " entities";
}

}  // namespace Vestige
