// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cutout_opening_command.h
/// @brief Undo command for cutting a door or window opening into a wall.
#pragma once

#include "editor/commands/editor_command.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace Vestige
{

class Mesh;
class Scene;

/// @brief Undo command for a `CutoutTool` opening (3D_E-0632).
///
/// The tool used to mutate the wall mesh in place and drop the
/// `CommandHistory&` it was handed, so a cut door or window could not be
/// undone. Worse, `FileMenu::isDirty()` delegates entirely to
/// `CommandHistory`, so the scene was never marked dirty either and quitting
/// discarded the cut with no unsaved-changes prompt.
///
/// The command holds the entity's id and looks it up on each execute and undo,
/// so an entity deleted while the command is on the undo stack makes both do
/// nothing instead of touching freed memory.
///
/// The mutation is two reversible writes — the renderer's mesh pointer and the
/// entity's name — so both old values are captured and swapped back on undo.
/// The old mesh is a `shared_ptr` the renderer already owns, so holding it
/// keeps it alive for the lifetime of the undo stack and costs one refcount.
///
/// This command APPLIES the mutation itself; the caller must not apply it
/// first. `CommandHistory::execute()` calls `execute()` unconditionally on the
/// first push rather than only on redo — the same trap `PlaceTreeCommand`
/// documents, where applying in the caller too made every placement happen
/// twice.
class CutoutOpeningCommand : public EditorCommand
{
public:
    CutoutOpeningCommand(Scene& scene,
                         uint32_t entityId,
                         std::shared_ptr<Mesh> oldMesh,
                         std::shared_ptr<Mesh> newMesh,
                         std::string oldName,
                         std::string newName,
                         std::string description)
        : m_scene(scene)
        , m_entityId(entityId)
        , m_oldMesh(std::move(oldMesh))
        , m_newMesh(std::move(newMesh))
        , m_oldName(std::move(oldName))
        , m_newName(std::move(newName))
        , m_description(std::move(description))
    {
    }

    void execute() override { apply(m_newMesh, m_newName); }

    void undo() override { apply(m_oldMesh, m_oldName); }

    std::string getDescription() const override { return m_description; }

private:
    void apply(const std::shared_ptr<Mesh>& mesh, const std::string& name);

    Scene& m_scene;
    uint32_t m_entityId = 0;
    std::shared_ptr<Mesh> m_oldMesh;
    std::shared_ptr<Mesh> m_newMesh;
    std::string m_oldName;
    std::string m_newName;
    std::string m_description;
};

} // namespace Vestige
