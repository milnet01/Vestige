// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file align_distribute_command.h
/// @brief Undoable align/distribute operations on multiple entities.
#pragma once

#include "editor/commands/editor_command.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Stores entity ID + old/new position for undoable batch alignment.
class AlignDistributeCommand : public EditorCommand
{
public:
    struct Entry
    {
        uint32_t entityId;
        glm::vec3 oldPosition;
        glm::vec3 newPosition;
    };

    AlignDistributeCommand(Scene& scene, std::string description,
                           std::vector<Entry> entries)
        : m_scene(scene)
        , m_description(std::move(description))
        , m_entries(std::move(entries))
    {
    }

    void execute() override
    {
        for (const auto& e : m_entries)
        {
            Entity* entity = m_scene.findEntityById(e.entityId);
            if (entity) entity->transform.position = e.newPosition;
        }
    }

    void undo() override
    {
        for (const auto& e : m_entries)
        {
            Entity* entity = m_scene.findEntityById(e.entityId);
            if (entity) entity->transform.position = e.oldPosition;
        }
    }

    std::string getDescription() const override
    {
        return m_description;
    }

private:
    Scene& m_scene;
    std::string m_description;
    std::vector<Entry> m_entries;
};

} // namespace Vestige
