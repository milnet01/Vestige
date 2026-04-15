// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file delete_entity_command.h
/// @brief Undoable entity deletion — takes ownership of the removed subtree.
#pragma once

#include "editor/commands/editor_command.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Removes an entity from the scene on execute, re-inserts on undo.
///
/// Uses ownership transfer: the removed Entity subtree is held as a
/// unique_ptr while "deleted". On undo, the same object (same ID) is
/// re-inserted at its original sibling position.
class DeleteEntityCommand : public EditorCommand
{
public:
    /// @brief Prepares to delete the given entity.
    /// @param scene Scene reference for ID lookup.
    /// @param entityId ID of the entity to delete.
    DeleteEntityCommand(Scene& scene, uint32_t entityId)
        : m_scene(scene)
        , m_entityId(entityId)
    {
        Entity* entity = scene.findEntityById(entityId);
        if (entity)
        {
            m_entityName = entity->getName();
        }
    }

    void execute() override
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        if (!entity)
        {
            return;
        }

        Entity* parent = entity->getParent();
        if (!parent)
        {
            return;
        }

        // Record parent ID and sibling index for undo reinsertion
        m_parentId = parent->getId();
        m_siblingIndex = findSiblingIndex(parent, entity);

        m_scene.unregisterEntityRecursive(entity);
        m_ownedEntity = parent->removeChild(entity);
    }

    void undo() override
    {
        if (!m_ownedEntity)
        {
            return;
        }

        Entity* parent = nullptr;
        if (m_parentId == 0)
        {
            parent = m_scene.getRoot();
        }
        else
        {
            parent = m_scene.findEntityById(m_parentId);
        }

        if (!parent)
        {
            // Parent was also deleted — fall back to root
            parent = m_scene.getRoot();
        }

        Entity* ptr = parent->insertChild(std::move(m_ownedEntity), m_siblingIndex);
        m_scene.registerEntityRecursive(ptr);
    }

    std::string getDescription() const override
    {
        return "Delete '" + m_entityName + "'";
    }

    uint32_t getEntityId() const { return m_entityId; }

private:
    static size_t findSiblingIndex(const Entity* parent, const Entity* child)
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

    Scene& m_scene;
    uint32_t m_entityId = 0;
    uint32_t m_parentId = 0;
    size_t m_siblingIndex = 0;
    std::string m_entityName;
    std::unique_ptr<Entity> m_ownedEntity;
};

} // namespace Vestige
