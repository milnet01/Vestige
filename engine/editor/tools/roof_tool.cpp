// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file roof_tool.cpp
/// @brief RoofTool implementation -- configure and place roof entities.
#include "editor/tools/roof_tool.h"
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

void RoofTool::activate()
{
    m_state = State::CONFIGURE;
    Logger::info("RoofTool: activated, configure roof parameters");
}

void RoofTool::cancel()
{
    m_state = State::INACTIVE;
    Logger::info("RoofTool: cancelled");
}

bool RoofTool::processClick(const glm::vec3& hitPoint, Scene& scene,
                            ResourceManager& resources, CommandHistory& history)
{
    if (m_state != State::PLACING)
    {
        return false;
    }

    // Create the roof entity at the click position
    Entity* entity = scene.createEntity("Roof");
    entity->transform.position = hitPoint;

    auto mesh = std::make_shared<Mesh>(
        ProceduralMeshBuilder::createRoof(roofType, roofWidth, roofDepth,
                                          peakHeight, overhang));

    auto material = resources.createMaterial(
        "__editor_mat_" + std::to_string(entity->getId()));
    material->setType(MaterialType::PBR);
    material->setAlbedo(glm::vec3(0.6f, 0.3f, 0.2f));  // Terracotta color
    material->setMetallic(0.0f);
    material->setRoughness(0.7f);
    entity->addComponent<MeshRenderer>(mesh, material);

    // Register with undo system
    auto cmd = std::make_unique<CreateEntityCommand>(scene, entity->getId());
    history.execute(std::move(cmd));

    // Determine type name for logging
    const char* typeNames[] = {"Flat", "Gable", "Shed"};
    int typeIndex = static_cast<int>(roofType);
    std::string typeName = (typeIndex >= 0 && typeIndex <= 2) ? typeNames[typeIndex] : "Unknown";

    Logger::info("RoofTool: placed " + typeName + " roof ("
                 + std::to_string(roofWidth) + "m x "
                 + std::to_string(roofDepth) + "m)");

    // Return to configure state for placing another roof
    m_state = State::CONFIGURE;
    return true;
}

void RoofTool::drawConfigPanel()
{
    if (m_state != State::CONFIGURE)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(300, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Roof Tool", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Roof Configuration");
        ImGui::Separator();

        // Roof type combo
        const char* typeNames[] = {"Flat", "Gable", "Shed"};
        int currentType = static_cast<int>(roofType);
        if (ImGui::Combo("Roof Type", &currentType, typeNames, 3))
        {
            roofType = static_cast<RoofType>(currentType);
        }

        ImGui::DragFloat("Width (m)", &roofWidth, 0.1f, 0.5f, 50.0f, "%.1f");
        ImGui::DragFloat("Depth (m)", &roofDepth, 0.1f, 0.5f, 50.0f, "%.1f");
        ImGui::DragFloat("Peak Height (m)", &peakHeight, 0.1f, 0.0f, 20.0f, "%.1f");
        ImGui::DragFloat("Overhang (m)", &overhang, 0.05f, 0.0f, 5.0f, "%.2f");

        ImGui::Separator();
        if (ImGui::Button("Place", ImVec2(120, 0)))
        {
            m_state = State::PLACING;
            Logger::info("RoofTool: click in viewport to place roof");
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
