// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file transform_command.h
/// @brief Undoable transform change (position, rotation, scale).
#pragma once

#include "editor/commands/editor_command.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Records a before/after transform for a single entity.
///
/// Created when a gizmo drag ends (begin/end bracketing) or when the
/// inspector commits a transform edit.
class TransformCommand : public EditorCommand
{
public:
    TransformCommand(Scene& scene, uint32_t entityId,
                     const glm::vec3& oldPos, const glm::vec3& oldRot, const glm::vec3& oldScale,
                     const glm::vec3& newPos, const glm::vec3& newRot, const glm::vec3& newScale)
        : m_scene(scene)
        , m_entityId(entityId)
        , m_oldPosition(oldPos), m_oldRotation(oldRot), m_oldScale(oldScale)
        , m_newPosition(newPos), m_newRotation(newRot), m_newScale(newScale)
    {
    }

    void execute() override
    {
        applyTransform(m_newPosition, m_newRotation, m_newScale);
    }

    void undo() override
    {
        applyTransform(m_oldPosition, m_oldRotation, m_oldScale);
    }

    std::string getDescription() const override
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        if (entity)
        {
            return "Transform '" + entity->getName() + "'";
        }
        return "Transform entity";
    }

    uint32_t getEntityId() const { return m_entityId; }

private:
    void applyTransform(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale)
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        if (!entity)
        {
            return;
        }

        entity->transform.position = pos;
        entity->transform.rotation = rot;
        entity->transform.scale = scale;

        if (entity->transform.hasMatrixOverride())
        {
            entity->transform.clearMatrixOverride();
        }
    }

    Scene& m_scene;
    uint32_t m_entityId;

    glm::vec3 m_oldPosition;
    glm::vec3 m_oldRotation;
    glm::vec3 m_oldScale;

    glm::vec3 m_newPosition;
    glm::vec3 m_newRotation;
    glm::vec3 m_newScale;
};

} // namespace Vestige
