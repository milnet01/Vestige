// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cutout_opening_command.cpp
/// @brief Undo command for cutting a door or window opening into a wall.

#include "editor/commands/cutout_opening_command.h"

#include "scene/entity.h"
#include "scene/mesh_renderer.h"

namespace Vestige
{

void CutoutOpeningCommand::apply(const std::shared_ptr<Mesh>& mesh,
                                 const std::string& name)
{
    // Both pointers are borrowed, not owned. An entity deleted while this
    // command sits on the undo stack would leave them dangling. The null
    // checks below only cover a null constructor argument; they cannot detect
    // a dangling pointer. Other commands look entities up by id instead.
    if (m_renderer != nullptr)
    {
        m_renderer->setMesh(mesh);
    }
    if (m_entity != nullptr)
    {
        m_entity->setName(name);
    }
}

} // namespace Vestige
