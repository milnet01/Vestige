// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cutout_opening_command.cpp
/// @brief Undo command for cutting a door or window opening into a wall.

#include "editor/commands/cutout_opening_command.h"

#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"

namespace Vestige
{

void CutoutOpeningCommand::apply(const std::shared_ptr<Mesh>& mesh,
                                 const std::string& name)
{
    // Look the entity up by id: it may have been deleted while this command
    // sat on the undo stack, in which case there is nothing to change.
    Entity* entity = m_scene.findEntityById(m_entityId);
    if (entity == nullptr)
    {
        return;
    }
    if (MeshRenderer* renderer = entity->getComponent<MeshRenderer>())
    {
        renderer->setMesh(mesh);
    }
    entity->setName(name);
}

} // namespace Vestige
