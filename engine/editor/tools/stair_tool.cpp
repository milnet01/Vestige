// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file stair_tool.cpp
/// @brief StairTool implementation -- configure and place stair entities.
#include "editor/tools/stair_tool.h"
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

#include <memory>
#include <string>

namespace Vestige
{

void StairTool::activate()
{
    m_state = State::CONFIGURE;
    Logger::info("StairTool: activated, configure stair parameters");
}

void StairTool::cancel()
{
    m_state = State::INACTIVE;
    Logger::info("StairTool: cancelled");
}

bool StairTool::processClick(const glm::vec3& hitPoint, Scene& scene,
                             ResourceManager& resources, CommandHistory& history)
{
    if (m_state != State::PLACING)
    {
        return false;
    }

    // A saved spiral like this would be refused on load and vanish
    // (3D_E-0967), so place none; back to the panel to fix the radii.
    if (stairType == StairType::SPIRAL && innerRadius >= outerRadius)
    {
        Logger::warning("StairTool: inner radius must be less than outer radius");
        m_state = State::CONFIGURE;
        return true;
    }

    // Create the stair entity at the click position
    std::string entityName = (stairType == StairType::STRAIGHT)
                                 ? "Stairs"
                                 : "Spiral Stairs";
    Entity* entity = scene.createEntity(entityName);
    entity->transform.position = hitPoint;

    // Generate the appropriate stair mesh
    std::shared_ptr<Mesh> mesh;
    if (stairType == StairType::STRAIGHT)
    {
        mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createStraightStairs(
                totalHeight, stepHeight, stepDepth, width));
    }
    else // SPIRAL
    {
        mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createSpiralStairs(
                totalHeight, stepHeight, innerRadius, outerRadius, totalAngle));
    }

    auto material = resources.createMaterial(
        "__editor_mat_" + std::to_string(entity->getId()));
    material->setType(MaterialType::PBR);
    material->setAlbedo(glm::vec3(0.7f));
    material->setMetallic(0.0f);
    material->setRoughness(0.5f);
    entity->addComponent<MeshRenderer>(mesh, material);

    // Register with undo system
    auto cmd = std::make_unique<CreateEntityCommand>(scene, entity->getId());
    history.execute(std::move(cmd));

    Logger::info("StairTool: placed " + entityName + " ("
                 + std::to_string(getStepCount()) + " steps, "
                 + std::to_string(totalHeight) + "m height)");

    // Return to configure state for placing more stairs
    m_state = State::CONFIGURE;
    return true;
}

void StairTool::beginPlacing()
{
    m_state = State::PLACING;
    Logger::info("StairTool: click in viewport to place stairs");
}

void StairTool::drawConfigPanel()
{
    if (m_state != State::CONFIGURE)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Stair Tool", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Stair Configuration");
        ImGui::Separator();

        // Stair type combo
        const char* typeNames[] = {"Straight", "Spiral"};
        int currentType = static_cast<int>(stairType);
        if (ImGui::Combo("Stair Type", &currentType, typeNames, 2))
        {
            stairType = static_cast<StairType>(currentType);
        }

        ImGui::DragFloat("Total Height (m)", &totalHeight, 0.1f, 0.5f, 30.0f, "%.1f");
        ImGui::DragFloat("Step Height (m)", &stepHeight, 0.01f, 0.05f, 0.5f, "%.2f");

        if (stairType == StairType::STRAIGHT)
        {
            ImGui::DragFloat("Step Depth (m)", &stepDepth, 0.01f, 0.1f, 1.0f, "%.2f");
            ImGui::DragFloat("Width (m)", &width, 0.05f, 0.3f, 10.0f, "%.2f");
        }
        else // SPIRAL
        {
            ImGui::DragFloat("Inner Radius (m)", &innerRadius, 0.05f, 0.1f, 5.0f, "%.2f");
            ImGui::DragFloat("Outer Radius (m)", &outerRadius, 0.05f, 0.3f, 10.0f, "%.2f");
            ImGui::DragFloat("Total Angle (deg)", &totalAngle, 5.0f, 90.0f, 1440.0f, "%.0f");
        }

        // Display calculated step count
        ImGui::Separator();
        ImGui::Text("Calculated steps: %d", getStepCount());

        ImGui::Separator();
        if (ImGui::Button("Place", ImVec2(120, 0)))
        {
            beginPlacing();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            cancel();
        }
    }
    ImGui::End();
}

int StairTool::getStepCount() const
{
    return ProceduralMeshBuilder::stairStepCount(totalHeight, stepHeight);
}

} // namespace Vestige
