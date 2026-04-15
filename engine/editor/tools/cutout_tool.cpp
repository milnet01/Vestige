// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cutout_tool.cpp
/// @brief CutoutTool implementation -- cut door/window openings in walls.
#include "editor/tools/cutout_tool.h"
#include "editor/commands/create_entity_command.h"
#include "editor/command_history.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "resource/resource_manager.h"
#include "renderer/material.h"
#include "utils/procedural_mesh.h"
#include "core/logger.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

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

    // Verify the entity has a MeshRenderer (i.e., it is a visible wall)
    auto* renderer = entity->getComponent<MeshRenderer>();
    if (!renderer)
    {
        Logger::warning("CutoutTool: selected entity has no MeshRenderer, cannot cut openings");
        return;
    }

    m_selectedWallId = entityId;
    m_state = State::CONFIGURE;
    Logger::info("CutoutTool: wall selected '" + entity->getName()
                 + "', configure opening parameters");
}

void CutoutTool::drawConfigDialog(Scene& scene, ResourceManager& /*resources*/,
                                  CommandHistory& /*history*/)
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
            auto* renderer = entity->getComponent<MeshRenderer>();
            if (renderer && renderer->getMesh())
            {
                // Get wall dimensions from the mesh AABB
                const AABB& bounds = renderer->getMesh()->getLocalBounds();
                glm::vec3 size = bounds.getSize();
                float wallWidth = size.x;
                float wallHeight = size.y;
                float wallThickness = size.z;

                // Clamp opening to fit within the wall
                float maxXOffset = wallWidth - openingWidth;
                if (maxXOffset < 0.0f)
                {
                    maxXOffset = 0.0f;
                }
                float xOffset = xPosition * maxXOffset;

                // Clamp sill + opening height to wall height
                if (sillHeight + openingHeight > wallHeight)
                {
                    openingHeight = wallHeight - sillHeight;
                    if (openingHeight < 0.1f)
                    {
                        openingHeight = 0.1f;
                        sillHeight = wallHeight - openingHeight;
                    }
                }

                // Build the opening
                WallOpening opening;
                opening.xOffset = xOffset;
                opening.yOffset = sillHeight;
                opening.width = openingWidth;
                opening.height = openingHeight;

                std::vector<WallOpening> openings;
                openings.push_back(opening);

                // Regenerate the wall mesh with the opening
                auto newMesh = std::make_shared<Mesh>(
                    ProceduralMeshBuilder::createWallWithOpenings(
                        wallWidth, wallHeight, wallThickness, openings));
                renderer->setMesh(newMesh);

                // Update entity name to reflect the opening
                std::string typeName = (openingType == OpeningType::DOOR)
                                           ? "Door"
                                           : "Window";
                entity->setName("Wall with " + typeName);

                Logger::info("CutoutTool: applied " + typeName + " opening ("
                             + std::to_string(openingWidth) + "m x "
                             + std::to_string(openingHeight) + "m)");
            }
            else
            {
                Logger::warning("CutoutTool: wall mesh not available");
            }
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
