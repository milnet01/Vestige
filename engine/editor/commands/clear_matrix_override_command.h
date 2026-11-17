// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file clear_matrix_override_command.h
/// @brief Undoable "Clear Matrix Override" for one entity.
#pragma once

#include "editor/commands/editor_command.h"
#include "scene/entity.h"
#include "scene/scene.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Vestige
{

/// @brief Clears an entity's direct matrix override (one imported from glTF,
///        say) so its position, rotation and scale apply again. Undo puts the
///        saved matrix back. TransformCommand cannot do this: it clears any
///        override whichever way it applies.
class ClearMatrixOverrideCommand : public EditorCommand
{
public:
    ClearMatrixOverrideCommand(Scene& scene, std::uint32_t entityId, const glm::mat4& overrideMatrix)
        : m_scene(scene)
        , m_entityId(entityId)
        , m_override(overrideMatrix)
    {
    }

    void execute() override
    {
        if (Entity* entity = m_scene.findEntityById(m_entityId))
        {
            entity->transform.clearMatrixOverride();
        }
    }

    void undo() override
    {
        if (Entity* entity = m_scene.findEntityById(m_entityId))
        {
            entity->transform.setLocalMatrix(m_override);
        }
    }

    std::string getDescription() const override
    {
        const Entity* entity = m_scene.findEntityById(m_entityId);
        return entity != nullptr ? "Clear matrix override on '" + entity->getName() + "'"
                                 : "Clear matrix override";
    }

private:
    Scene& m_scene;
    std::uint32_t m_entityId;
    glm::mat4 m_override;
};

} // namespace Vestige
