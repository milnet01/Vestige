// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file create_entity_command.h
/// @brief Undoable entity creation — undo removes, redo re-inserts.
#pragma once

#include "editor/commands/editor_command.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Wraps an already-created entity so it can be undone.
///
/// On first execute() the entity already exists in the scene (created by
/// EntityFactory or Scene::duplicateEntity), so execute() is a no-op.
/// On undo(), the entity is detached and stored. On redo(), it is re-inserted.
class CreateEntityCommand : public EditorCommand
{
public:
    /// @brief Wraps an entity that was just created in the scene.
    /// @param scene Scene reference for ID lookup.
    /// @param entityId ID of the already-created entity.
    CreateEntityCommand(Scene& scene, uint32_t entityId)
        : m_scene(scene)
        , m_entityId(entityId)
    {
        // Record parent, name, and sibling index for later redo placement.
        // AUDIT Ed6 — capturing the original sibling index here (and again
        // in undo()) lets execute() reinsert at that exact position so an
        // undo→redo round-trip preserves sibling order. Pre-Ed6 redo
        // appended via addChild and silently bumped every later sibling.
        Entity* entity = scene.findEntityById(entityId);
        if (entity)
        {
            m_entityName = entity->getName();
            Entity* parent = entity->getParent();
            m_parentId = parent ? parent->getId() : 0;
            if (parent)
            {
                m_siblingIndex = findSiblingIndex(parent, entity);
            }
        }
    }

    void execute() override
    {
        // On first call, entity already exists — no-op.
        // On redo (after undo), re-insert from owned storage at the
        // recorded sibling index.
        if (m_ownedEntity)
        {
            Entity* parent = nullptr;
            if (m_parentId == 0)
            {
                parent = m_scene.getRoot();
            }
            else
            {
                parent = m_scene.findEntityById(m_parentId);
            }

            if (parent)
            {
                Entity* ptr = parent->insertChild(
                    std::move(m_ownedEntity), m_siblingIndex);
                m_scene.registerEntityRecursive(ptr);
            }
        }
    }

    void undo() override
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

        // Refresh the recorded sibling index in case earlier commands have
        // moved the entity since construction.
        m_parentId = parent->getId();
        m_siblingIndex = findSiblingIndex(parent, entity);
        m_scene.unregisterEntityRecursive(entity);
        m_ownedEntity = parent->removeChild(entity);
    }

    std::string getDescription() const override
    {
        return "Create '" + m_entityName + "'";
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
