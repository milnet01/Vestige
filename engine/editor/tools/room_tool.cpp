/// @file room_tool.cpp
/// @brief RoomTool implementation -- rectangular room creation.
#include "editor/tools/room_tool.h"
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

namespace Vestige
{

namespace
{

/// @brief Creates a unique PBR material for a room child entity.
std::shared_ptr<Material> createRoomMaterial(ResourceManager& resources,
                                             uint32_t entityId,
                                             const glm::vec3& albedo = glm::vec3(0.7f))
{
    std::string name = "__editor_mat_" + std::to_string(entityId);
    auto material = resources.createMaterial(name);
    material->setType(MaterialType::PBR);
    material->setAlbedo(albedo);
    material->setMetallic(0.0f);
    material->setRoughness(0.5f);
    return material;
}

} // anonymous namespace

void RoomTool::activateDimensionMode()
{
    m_state = State::DIMENSION_INPUT;
    Logger::info("RoomTool: dimension input mode activated");
}

void RoomTool::activateClickMode()
{
    m_state = State::WAITING_CORNER;
    Logger::info("RoomTool: click-to-place mode activated, click to set room corner");
}

void RoomTool::cancel()
{
    m_state = State::INACTIVE;
    Logger::info("RoomTool: cancelled");
}

bool RoomTool::processClick(const glm::vec3& hitPoint, Scene& scene,
                            ResourceManager& resources, CommandHistory& history)
{
    if (m_state != State::WAITING_CORNER)
    {
        return false;
    }

    createRoom(hitPoint, scene, resources, history);

    // Stay in click mode for placing more rooms
    Logger::info("RoomTool: room placed, click to place another");
    return true;
}

void RoomTool::drawDimensionDialog(Scene& scene, ResourceManager& resources,
                                   CommandHistory& history)
{
    if (m_state != State::DIMENSION_INPUT)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(300, 280), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Room Tool", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Room Dimensions");
        ImGui::Separator();

        ImGui::DragFloat("Width (m)", &roomWidth, 0.1f, 0.5f, 50.0f, "%.1f");
        ImGui::DragFloat("Depth (m)", &roomDepth, 0.1f, 0.5f, 50.0f, "%.1f");
        ImGui::DragFloat("Height (m)", &roomHeight, 0.1f, 0.5f, 20.0f, "%.1f");
        ImGui::DragFloat("Wall Thickness (m)", &wallThickness, 0.01f, 0.05f, 1.0f, "%.2f");

        ImGui::Separator();
        ImGui::Checkbox("Include Floor", &includeFloor);
        ImGui::Checkbox("Include Ceiling", &includeCeiling);

        ImGui::Separator();
        ImGui::Text("Position");
        ImGui::DragFloat3("##pos", &m_clickPosition.x, 0.1f);

        ImGui::Separator();
        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            createRoom(m_clickPosition, scene, resources, history);
        }
        ImGui::SameLine();
        if (ImGui::Button("Place by Click", ImVec2(120, 0)))
        {
            m_state = State::WAITING_CORNER;
            Logger::info("RoomTool: switched to click-to-place mode");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            cancel();
        }
    }
    ImGui::End();
}

void RoomTool::queueDebugDraw() const
{
    // No preview visualization for room tool
}

Entity* RoomTool::createRoom(const glm::vec3& position, Scene& scene,
                             ResourceManager& resources, CommandHistory& history)
{
    float halfW = roomWidth * 0.5f;
    float halfD = roomDepth * 0.5f;

    // Create the parent group entity
    Entity* group = scene.createEntity("Room");
    group->transform.position = position;

    // Front wall (+Z face)
    {
        Entity* wall = scene.createEntity("Front Wall");
        auto mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createWall(roomWidth, roomHeight, wallThickness));
        auto material = createRoomMaterial(resources, wall->getId());
        wall->addComponent<MeshRenderer>(mesh, material);
        wall->transform.position = glm::vec3(0.0f, 0.0f, halfD);
        scene.reparentEntity(wall->getId(), group->getId());
    }

    // Back wall (-Z face)
    {
        Entity* wall = scene.createEntity("Back Wall");
        auto mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createWall(roomWidth, roomHeight, wallThickness));
        auto material = createRoomMaterial(resources, wall->getId());
        wall->addComponent<MeshRenderer>(mesh, material);
        wall->transform.position = glm::vec3(0.0f, 0.0f, -halfD);
        scene.reparentEntity(wall->getId(), group->getId());
    }

    // Left wall (-X face), rotated 90 degrees around Y
    {
        Entity* wall = scene.createEntity("Left Wall");
        auto mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createWall(roomDepth, roomHeight, wallThickness));
        auto material = createRoomMaterial(resources, wall->getId());
        wall->addComponent<MeshRenderer>(mesh, material);
        wall->transform.position = glm::vec3(-halfW, 0.0f, 0.0f);
        wall->transform.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
        scene.reparentEntity(wall->getId(), group->getId());
    }

    // Right wall (+X face), rotated 90 degrees around Y
    {
        Entity* wall = scene.createEntity("Right Wall");
        auto mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createWall(roomDepth, roomHeight, wallThickness));
        auto material = createRoomMaterial(resources, wall->getId());
        wall->addComponent<MeshRenderer>(mesh, material);
        wall->transform.position = glm::vec3(halfW, 0.0f, 0.0f);
        wall->transform.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
        scene.reparentEntity(wall->getId(), group->getId());
    }

    // Optional floor
    if (includeFloor)
    {
        Entity* floor = scene.createEntity("Floor");
        auto mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createFloor(roomWidth, roomDepth, 0.15f));
        auto material = createRoomMaterial(resources, floor->getId(), glm::vec3(0.5f));
        floor->addComponent<MeshRenderer>(mesh, material);
        scene.reparentEntity(floor->getId(), group->getId());
    }

    // Optional ceiling
    if (includeCeiling)
    {
        Entity* ceiling = scene.createEntity("Ceiling");
        auto mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createFloor(roomWidth, roomDepth, 0.15f));
        auto material = createRoomMaterial(resources, ceiling->getId(), glm::vec3(0.6f));
        ceiling->addComponent<MeshRenderer>(mesh, material);
        ceiling->transform.position = glm::vec3(0.0f, roomHeight, 0.0f);
        scene.reparentEntity(ceiling->getId(), group->getId());
    }

    // Register with undo system
    auto cmd = std::make_unique<CreateEntityCommand>(scene, group->getId());
    history.execute(std::move(cmd));

    Logger::info("RoomTool: created room (" + std::to_string(roomWidth)
                 + "m x " + std::to_string(roomDepth)
                 + "m x " + std::to_string(roomHeight) + "m)");
    return group;
}

} // namespace Vestige
