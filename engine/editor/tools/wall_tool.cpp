// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file wall_tool.cpp
/// @brief WallTool implementation -- interactive two-click wall placement.
#include "editor/tools/wall_tool.h"
#include "editor/commands/create_entity_command.h"
#include "editor/command_history.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "resource/resource_manager.h"
#include "renderer/material.h"
#include "renderer/debug_draw.h"
#include "utils/procedural_mesh.h"
#include "core/logger.h"

#include <cmath>
#include <memory>
#include <string>

namespace Vestige
{

void WallTool::activate()
{
    m_state = State::WAITING_START;
    m_startPoint = glm::vec3(0.0f);
    Logger::info("WallTool: activated, click to set start point");
}

void WallTool::cancel()
{
    m_state = State::INACTIVE;
    Logger::info("WallTool: cancelled");
}

bool WallTool::processClick(const glm::vec3& hitPoint, Scene& scene,
                            ResourceManager& resources, CommandHistory& history)
{
    if (m_state == State::WAITING_START)
    {
        m_startPoint = hitPoint;
        m_state = State::WAITING_END;
        Logger::info("WallTool: start point set, click to set end point");
        return true;
    }
    else if (m_state == State::WAITING_END)
    {
        glm::vec3 endPoint = hitPoint;

        // Calculate wall dimensions from the two click points
        float dx = endPoint.x - m_startPoint.x;
        float dz = endPoint.z - m_startPoint.z;
        float wallWidth = std::sqrt(dx * dx + dz * dz);

        // Avoid degenerate walls
        if (wallWidth < 0.01f)
        {
            Logger::warning("WallTool: wall too short, click a different point");
            return true;
        }

        // Calculate rotation angle (Y-axis) from the wall direction
        float angle = glm::degrees(std::atan2(dx, dz));

        // Calculate midpoint for wall position
        glm::vec3 midpoint = (m_startPoint + endPoint) * 0.5f;
        midpoint.y = height * 0.5f;

        // Create the wall entity
        Entity* entity = scene.createEntity("Wall");
        entity->transform.position = midpoint;
        entity->transform.rotation.y = angle;

        // Generate wall mesh and material
        auto mesh = std::make_shared<Mesh>(
            ProceduralMeshBuilder::createWall(wallWidth, height, thickness));
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

        Logger::info("WallTool: placed wall (" + std::to_string(wallWidth)
                     + "m x " + std::to_string(height) + "m)");

        // Stay active for the next wall, using the end point as the new start
        m_startPoint = endPoint;
        m_state = State::WAITING_END;
        return true;
    }

    return false;
}

void WallTool::queueDebugDraw(const glm::vec3& currentHit) const
{
    if (m_state != State::WAITING_END)
    {
        return;
    }

    glm::vec3 color(0.0f, 1.0f, 1.0f); // Cyan preview line

    // Draw the preview line from start to current cursor position
    DebugDraw::line(m_startPoint, currentHit, color);

    // Draw markers at the start point
    float markerSize = 0.15f;
    DebugDraw::line(m_startPoint - glm::vec3(markerSize, 0.0f, 0.0f),
                    m_startPoint + glm::vec3(markerSize, 0.0f, 0.0f), color);
    DebugDraw::line(m_startPoint - glm::vec3(0.0f, markerSize, 0.0f),
                    m_startPoint + glm::vec3(0.0f, markerSize, 0.0f), color);
    DebugDraw::line(m_startPoint - glm::vec3(0.0f, 0.0f, markerSize),
                    m_startPoint + glm::vec3(0.0f, 0.0f, markerSize), color);

    // Draw a rectangle preview showing wall height at the current position
    glm::vec3 topStart = m_startPoint + glm::vec3(0.0f, height, 0.0f);
    glm::vec3 topEnd = currentHit + glm::vec3(0.0f, height, 0.0f);
    DebugDraw::line(topStart, topEnd, color);
    DebugDraw::line(m_startPoint, topStart, color);
    DebugDraw::line(currentHit, topEnd, color);
}

} // namespace Vestige
