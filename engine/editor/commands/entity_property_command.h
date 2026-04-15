// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file entity_property_command.h
/// @brief Undoable entity property changes — name, visible, locked, active.
#pragma once

#include "editor/commands/editor_command.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <string>

namespace Vestige
{

/// @brief Which entity property is being changed.
enum class EntityProperty
{
    NAME,
    VISIBLE,
    LOCKED,
    ACTIVE
};

/// @brief Records a before/after change to a single entity property.
///
/// Handles both string properties (name) and boolean properties
/// (visible, locked, active) via separate old/new storage.
class EntityPropertyCommand : public EditorCommand
{
public:
    /// @brief Constructor for NAME property.
    EntityPropertyCommand(Scene& scene, uint32_t entityId,
                          EntityProperty prop,
                          const std::string& oldValue,
                          const std::string& newValue)
        : m_scene(scene)
        , m_entityId(entityId)
        , m_property(prop)
        , m_oldString(oldValue)
        , m_newString(newValue)
    {
    }

    /// @brief Constructor for VISIBLE / LOCKED / ACTIVE properties.
    EntityPropertyCommand(Scene& scene, uint32_t entityId,
                          EntityProperty prop,
                          bool oldValue, bool newValue)
        : m_scene(scene)
        , m_entityId(entityId)
        , m_property(prop)
        , m_oldBool(oldValue)
        , m_newBool(newValue)
    {
    }

    void execute() override
    {
        applyValues(m_newString, m_newBool);
    }

    void undo() override
    {
        applyValues(m_oldString, m_oldBool);
    }

    std::string getDescription() const override
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        std::string name = entity ? entity->getName() : "entity";

        switch (m_property)
        {
            case EntityProperty::NAME:
                return "Rename '" + m_oldString + "' -> '" + m_newString + "'";
            case EntityProperty::VISIBLE:
                return (m_newBool ? "Show" : "Hide") + std::string(" '") + name + "'";
            case EntityProperty::LOCKED:
                return (m_newBool ? "Lock" : "Unlock") + std::string(" '") + name + "'";
            case EntityProperty::ACTIVE:
                return (m_newBool ? "Activate" : "Deactivate") + std::string(" '") + name + "'";
        }
        return "Change property";
    }

    uint32_t getEntityId() const { return m_entityId; }

private:
    void applyValues(const std::string& strVal, bool boolVal)
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        if (!entity)
        {
            return;
        }

        switch (m_property)
        {
            case EntityProperty::NAME:
                entity->setName(strVal);
                break;
            case EntityProperty::VISIBLE:
                entity->setVisible(boolVal);
                break;
            case EntityProperty::LOCKED:
                entity->setLocked(boolVal);
                break;
            case EntityProperty::ACTIVE:
                entity->setActive(boolVal);
                break;
        }
    }

    Scene& m_scene;
    uint32_t m_entityId;
    EntityProperty m_property;

    std::string m_oldString;
    std::string m_newString;
    bool m_oldBool = false;
    bool m_newBool = false;
};

} // namespace Vestige
