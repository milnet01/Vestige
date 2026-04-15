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
        // Record parent and name for later use
        Entity* entity = scene.findEntityById(entityId);
        if (entity)
        {
            m_entityName = entity->getName();
            Entity* parent = entity->getParent();
            m_parentId = parent ? parent->getId() : 0;
        }
    }

    void execute() override
    {
        // On first call, entity already exists — no-op.
        // On redo (after undo), re-insert from owned storage.
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
                Entity* ptr = parent->addChild(std::move(m_ownedEntity));
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

        // Record sibling index for potential future use
        m_parentId = parent->getId();
        m_scene.unregisterEntityRecursive(entity);
        m_ownedEntity = parent->removeChild(entity);
    }

    std::string getDescription() const override
    {
        return "Create '" + m_entityName + "'";
    }

    uint32_t getEntityId() const { return m_entityId; }

private:
    Scene& m_scene;
    uint32_t m_entityId = 0;
    uint32_t m_parentId = 0;
    std::string m_entityName;
    std::unique_ptr<Entity> m_ownedEntity;
};

} // namespace Vestige
