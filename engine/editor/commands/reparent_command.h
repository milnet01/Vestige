/// @file reparent_command.h
/// @brief Undoable entity reparenting — move entity to a new parent.
#pragma once

#include "editor/commands/editor_command.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <string>

namespace Vestige
{

/// @brief Moves an entity from one parent to another, with full undo support.
///
/// On execute, the entity is removed from its current parent and added to
/// the new parent. On undo, it is restored at its original parent and
/// sibling index.
class ReparentCommand : public EditorCommand
{
public:
    /// @brief Prepares to reparent an entity.
    /// @param scene Scene reference for ID lookup.
    /// @param entityId Entity to reparent.
    /// @param newParentId New parent (0 = scene root).
    ReparentCommand(Scene& scene, uint32_t entityId, uint32_t newParentId)
        : m_scene(scene)
        , m_entityId(entityId)
        , m_newParentId(newParentId)
    {
        // Capture old parent info before execute changes anything
        Entity* entity = scene.findEntityById(entityId);
        if (entity)
        {
            m_entityName = entity->getName();
            Entity* parent = entity->getParent();
            if (parent)
            {
                m_oldParentId = parent->getId();
                m_oldSiblingIndex = findSiblingIndex(parent, entity);
            }
        }
    }

    void execute() override
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        if (!entity || !entity->getParent())
        {
            return;
        }

        Entity* newParent = (m_newParentId == 0)
            ? m_scene.getRoot()
            : m_scene.findEntityById(m_newParentId);

        if (!newParent)
        {
            return;
        }

        Entity* oldParent = entity->getParent();
        if (oldParent == newParent)
        {
            return;
        }

        // Prevent circular reparenting
        if (isDescendantOf(newParent, entity))
        {
            return;
        }

        auto owned = oldParent->removeChild(entity);
        if (owned)
        {
            newParent->addChild(std::move(owned));
        }
    }

    void undo() override
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        if (!entity || !entity->getParent())
        {
            return;
        }

        Entity* currentParent = entity->getParent();
        Entity* oldParent = (m_oldParentId == 0)
            ? m_scene.getRoot()
            : m_scene.findEntityById(m_oldParentId);

        if (!oldParent)
        {
            oldParent = m_scene.getRoot();
        }

        auto owned = currentParent->removeChild(entity);
        if (owned)
        {
            oldParent->insertChild(std::move(owned), m_oldSiblingIndex);
        }
    }

    std::string getDescription() const override
    {
        return "Reparent '" + m_entityName + "'";
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

    static bool isDescendantOf(Entity* potential, Entity* ancestor)
    {
        Entity* current = potential;
        while (current)
        {
            if (current == ancestor)
            {
                return true;
            }
            current = current->getParent();
        }
        return false;
    }

    Scene& m_scene;
    uint32_t m_entityId = 0;
    uint32_t m_oldParentId = 0;
    size_t m_oldSiblingIndex = 0;
    uint32_t m_newParentId = 0;
    std::string m_entityName;
};

} // namespace Vestige
