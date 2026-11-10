// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cutout_tool.cpp
/// @brief CutoutTool implementation -- cut door/window openings in walls.
#include "editor/tools/cutout_tool.h"
#include "editor/command_history.h"
#include "editor/commands/cutout_opening_command.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "resource/resource_manager.h"
#include "utils/procedural_mesh.h"
#include "core/logger.h"

#include <imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

namespace
{

// The WALL recipe of the entity's mesh; nullptr when it has none.
const MeshRecipe* wallRecipe(const Entity& entity)
{
    const auto* renderer = entity.getComponent<MeshRenderer>();
    if (renderer == nullptr || !renderer->getMesh())
    {
        return nullptr;
    }
    const MeshRecipe* recipe = renderer->getMesh()->getRecipe();
    if (recipe == nullptr || recipe->generator != MeshRecipe::Generator::WALL)
    {
        return nullptr;
    }
    return recipe;
}

}  // namespace

void CutoutTool::activate()
{
    m_state = State::SELECT_WALL;
    m_selectedWallId = 0;
    Logger::info("CutoutTool: activated, select a wall entity");
}

void CutoutTool::cancel()
{
    m_state = State::INACTIVE;
    m_selectedWallId = 0;
    Logger::info("CutoutTool: cancelled");
}

void CutoutTool::selectWall(uint32_t entityId, Scene& scene)
{
    if (m_state != State::SELECT_WALL)
    {
        return;
    }

    Entity* entity = scene.findEntityById(entityId);
    if (!entity)
    {
        Logger::warning("CutoutTool: entity not found (ID " + std::to_string(entityId) + ")");
        return;
    }

    // Only a wall the builder made can be cut: its recipe holds the size and
    // the openings already in it. Anything else (a cube, a model) would be
    // replaced by a box wall on Apply.
    if (wallRecipe(*entity) == nullptr)
    {
        Logger::warning("CutoutTool: '" + entity->getName()
                        + "' is not a built wall, cannot cut openings");
        return;
    }

    m_selectedWallId = entityId;
    m_state = State::CONFIGURE;
    Logger::info("CutoutTool: wall selected '" + entity->getName()
                 + "', configure opening parameters");
}

void CutoutTool::applyOpening(Scene& scene, CommandHistory& history)
{
    if (m_state != State::CONFIGURE)
    {
        return;
    }
    Entity* entity = scene.findEntityById(m_selectedWallId);
    const MeshRecipe* wall = entity ? wallRecipe(*entity) : nullptr;
    if (wall == nullptr)
    {
        Logger::warning("CutoutTool: wall mesh not available");
        return;
    }
    // A saved wall with more openings than this would be refused on load.
    if (wall->openings.size() >= MeshRecipe::MAX_OPENINGS)
    {
        Logger::warning("CutoutTool: a wall holds at most "
                        + std::to_string(MeshRecipe::MAX_OPENINGS) + " openings");
        return;
    }

    // Clamp opening to fit within the wall
    float maxXOffset = wall->width - openingWidth;
    if (maxXOffset < 0.0f)
    {
        maxXOffset = 0.0f;
    }
    float xOffset = xPosition * maxXOffset;

    // Clamp sill + opening height to wall height
    if (sillHeight + openingHeight > wall->height)
    {
        openingHeight = wall->height - sillHeight;
        if (openingHeight < 0.1f)
        {
            openingHeight = 0.1f;
            sillHeight = wall->height - openingHeight;
        }
    }

    WallOpening opening;
    opening.xOffset = xOffset;
    opening.yOffset = sillHeight;
    opening.width = openingWidth;
    opening.height = openingHeight;

    // 3D_E-0967: rebuild from every opening the wall already has, plus this
    // one, so a second cut keeps the first.
    std::vector<WallOpening> openings = wall->openings;
    openings.push_back(opening);
    auto newMesh = std::make_shared<Mesh>(ProceduralMeshBuilder::createWallWithOpenings(
        wall->width, wall->height, wall->thickness, openings));

    std::string typeName = (openingType == OpeningType::DOOR) ? "Door" : "Window";

    // 3D_E-0632: go through the undo stack rather than mutating in place.
    // Beyond undo itself, FileMenu::isDirty() delegates entirely to
    // CommandHistory -- so before this, cutting an opening left the scene
    // "clean" and quitting discarded it with no unsaved-changes prompt.
    //
    // The command APPLIES the change; do not also apply it here.
    // CommandHistory::execute() runs execute() on the first push, not only on
    // redo (see place_tree_command.h, where doing both placed every tree twice).
    history.execute(std::make_unique<CutoutOpeningCommand>(
        scene,
        entity->getId(),
        entity->getComponent<MeshRenderer>()->getMesh(),
        newMesh,
        entity->getName(),
        "Wall with " + typeName,
        "Cut " + typeName + " opening"));

    Logger::info("CutoutTool: applied " + typeName + " opening ("
                 + std::to_string(openingWidth) + "m x "
                 + std::to_string(openingHeight) + "m)");
}

void CutoutTool::drawConfigDialog(Scene& scene, ResourceManager& /*resources*/,
                                  CommandHistory& history)
{
    if (m_state != State::CONFIGURE)
    {
        return;
    }

    Entity* entity = scene.findEntityById(m_selectedWallId);
    if (!entity)
    {
        Logger::warning("CutoutTool: selected wall no longer exists");
        cancel();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(320, 280), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Cutout Tool", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Wall: %s", entity->getName().c_str());
        ImGui::Separator();

        // Opening type combo
        const char* typeNames[] = {"Door", "Window"};
        int currentType = static_cast<int>(openingType);
        if (ImGui::Combo("Opening Type", &currentType, typeNames, 2))
        {
            openingType = static_cast<OpeningType>(currentType);

            // Apply default values when switching type
            if (openingType == OpeningType::DOOR)
            {
                openingWidth = 0.9f;
                openingHeight = 2.1f;
                sillHeight = 0.0f;
            }
            else // WINDOW
            {
                openingWidth = 1.0f;
                openingHeight = 1.2f;
                sillHeight = 0.9f;
            }
        }

        ImGui::Separator();
        ImGui::DragFloat("Width (m)", &openingWidth, 0.05f, 0.3f, 10.0f, "%.2f");
        ImGui::DragFloat("Height (m)", &openingHeight, 0.05f, 0.3f, 10.0f, "%.2f");
        ImGui::DragFloat("Sill Height (m)", &sillHeight, 0.05f, 0.0f, 10.0f, "%.2f");
        ImGui::DragFloat("X Position (0-1)", &xPosition, 0.01f, 0.0f, 1.0f, "%.2f");

        ImGui::Separator();
        if (ImGui::Button("Apply", ImVec2(120, 0)))
        {
            applyOpening(scene, history);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            cancel();
        }
    }
    ImGui::End();
}

} // namespace Vestige
