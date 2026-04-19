// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file path_tool.cpp
/// @brief PathTool implementation -- interactive spline-based path/road creation.
#include "editor/tools/path_tool.h"
#include "editor/commands/create_entity_command.h"
#include "editor/command_history.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "resource/resource_manager.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/debug_draw.h"
#include "core/logger.h"

#include <imgui.h>

#include <memory>
#include <string>

namespace Vestige
{

void PathTool::activate()
{
    m_spline.clear();
    m_state = State::PLACING_POINTS;
    Logger::info("PathTool: activated, click to place waypoints");
}

void PathTool::cancel()
{
    m_spline.clear();
    m_state = State::INACTIVE;
    Logger::info("PathTool: cancelled");
}

bool PathTool::processClick(const glm::vec3& hitPoint)
{
    if (m_state != State::PLACING_POINTS)
    {
        return false;
    }

    m_spline.addPoint(hitPoint);
    Logger::info("PathTool: added waypoint #" + std::to_string(m_spline.getPointCount())
                 + " at (" + std::to_string(hitPoint.x) + ", "
                 + std::to_string(hitPoint.y) + ", "
                 + std::to_string(hitPoint.z) + ")");
    return true;
}

bool PathTool::finishPath(Scene& scene, ResourceManager& resources,
                          CommandHistory& history)
{
    if (m_spline.getPointCount() < 2)
    {
        Logger::warning("PathTool: need at least 2 waypoints to create a path");
        return false;
    }

    generatePathMesh(scene, resources, history);
    m_spline.clear();
    m_state = State::INACTIVE;
    return true;
}

void PathTool::queueDebugDraw(const glm::vec3& currentHit) const
{
    if (m_state != State::PLACING_POINTS)
    {
        return;
    }

    glm::vec3 yellow(1.0f, 1.0f, 0.0f);
    glm::vec3 green(0.0f, 1.0f, 0.0f);
    glm::vec3 cyan(0.0f, 1.0f, 1.0f);
    float markerSize = 0.15f;

    // Draw straight lines between control points (yellow)
    for (size_t i = 0; i < m_spline.getPointCount(); ++i)
    {
        const glm::vec3& pt = m_spline.getPoint(i);

        // Draw cross marker at each control point
        DebugDraw::line(pt - glm::vec3(markerSize, 0.0f, 0.0f),
                        pt + glm::vec3(markerSize, 0.0f, 0.0f), yellow);
        DebugDraw::line(pt - glm::vec3(0.0f, markerSize, 0.0f),
                        pt + glm::vec3(0.0f, markerSize, 0.0f), yellow);
        DebugDraw::line(pt - glm::vec3(0.0f, 0.0f, markerSize),
                        pt + glm::vec3(0.0f, 0.0f, markerSize), yellow);

        // Draw yellow line to next control point
        if (i + 1 < m_spline.getPointCount())
        {
            DebugDraw::line(pt, m_spline.getPoint(i + 1), yellow);
        }
    }

    // Draw line from last control point to current cursor position
    if (m_spline.getPointCount() > 0)
    {
        DebugDraw::line(m_spline.getPoint(m_spline.getPointCount() - 1),
                        currentHit, yellow);
    }

    // Draw the smooth spline curve (green) if we have enough points
    if (m_spline.getPointCount() >= 2)
    {
        std::vector<glm::vec3> samples = m_spline.sample(samplesPerSegment);

        for (size_t i = 1; i < samples.size(); ++i)
        {
            DebugDraw::line(samples[i - 1], samples[i], green);
        }

        // Draw width indicators at each sample point (cyan)
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        float maxT = static_cast<float>(m_spline.getPointCount() - 1);
        int totalSamples = static_cast<int>((m_spline.getPointCount() - 1))
                           * samplesPerSegment + 1;

        for (int i = 0; i < totalSamples; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(samplesPerSegment);
            t = glm::clamp(t, 0.0f, maxT);

            glm::vec3 pos = m_spline.evaluate(t);
            glm::vec3 tangent = m_spline.evaluateTangent(t);
            float tangentLen = glm::length(tangent);

            if (tangentLen < 0.0001f)
            {
                continue;
            }

            tangent = glm::normalize(tangent);
            glm::vec3 right = glm::normalize(glm::cross(tangent, up));

            glm::vec3 left = pos - right * (pathWidth * 0.5f);
            glm::vec3 rightEdge = pos + right * (pathWidth * 0.5f);
            DebugDraw::line(left, rightEdge, cyan);
        }
    }
}

void PathTool::drawConfigPanel()
{
    if (m_state == State::INACTIVE)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(300, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Path Tool", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Path Configuration");
        ImGui::Separator();

        // Preset combo
        const char* presetNames[] = {"Narrow Footpath", "Wide Road", "Stone Walkway"};
        int currentPreset = static_cast<int>(preset);
        if (ImGui::Combo("Preset", &currentPreset, presetNames, 3))
        {
            preset = static_cast<PathPreset>(currentPreset);
            applyPreset(preset);
        }

        ImGui::DragFloat("Width (m)", &pathWidth, 0.1f, 0.2f, 20.0f, "%.1f");
        ImGui::DragInt("Samples/Segment", &samplesPerSegment, 1, 2, 50);

        ImGui::Separator();
        ImGui::Text("Waypoints placed: %zu", m_spline.getPointCount());

        if (m_spline.getPointCount() >= 2)
        {
            float approxLength = m_spline.getApproxLength(samplesPerSegment);
            ImGui::Text("Approx. length: %.1f m", static_cast<double>(approxLength));
        }

        ImGui::Separator();

        // Finish Path button (enabled when >= 2 points)
        if (m_spline.getPointCount() >= 2)
        {
            if (ImGui::Button("Finish Path", ImVec2(120, 0)))
            {
                m_state = State::FINISHED;
            }
            ImGui::SameLine();
        }

        // Undo last point button
        if (m_spline.getPointCount() > 0)
        {
            if (ImGui::Button("Undo Last Point", ImVec2(140, 0)))
            {
                // Rebuild spline without the last point
                std::vector<glm::vec3> points;
                for (size_t i = 0; i < m_spline.getPointCount() - 1; ++i)
                {
                    points.push_back(m_spline.getPoint(i));
                }
                m_spline.clear();
                for (const auto& p : points)
                {
                    m_spline.addPoint(p);
                }
                Logger::info("PathTool: removed last waypoint");
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            cancel();
        }
    }
    ImGui::End();
}

void PathTool::applyPreset(PathPreset p)
{
    switch (p)
    {
        case PathPreset::NARROW_FOOTPATH:
            pathWidth = 1.0f;
            break;
        case PathPreset::WIDE_ROAD:
            pathWidth = 4.0f;
            break;
        case PathPreset::STONE_WALKWAY:
            pathWidth = 2.0f;
            break;
    }
}

void PathTool::generatePathMesh(Scene& scene, ResourceManager& resources,
                                CommandHistory& history)
{
    // Sample the spline
    std::vector<glm::vec3> samples = m_spline.sample(samplesPerSegment);

    if (samples.size() < 2)
    {
        Logger::warning("PathTool: not enough sample points to generate mesh");
        return;
    }

    glm::vec3 up(0.0f, 1.0f, 0.0f);

    // Build vertex and index data
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    size_t sampleCount = samples.size();
    vertices.reserve(sampleCount * 2);

    // Calculate cumulative distances for UV v-coordinate
    std::vector<float> cumulativeDistances;
    cumulativeDistances.reserve(sampleCount);
    cumulativeDistances.push_back(0.0f);

    for (size_t i = 1; i < sampleCount; ++i)
    {
        float dist = glm::distance(samples[i - 1], samples[i]);
        cumulativeDistances.push_back(cumulativeDistances[i - 1] + dist);
    }

    float totalLength = cumulativeDistances.back();

    // Generate left and right edge vertices for each sample point
    float maxT = static_cast<float>(m_spline.getPointCount() - 1);

    for (size_t i = 0; i < sampleCount; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(samplesPerSegment);
        t = glm::clamp(t, 0.0f, maxT);

        glm::vec3 tangent = m_spline.evaluateTangent(t);
        float tangentLen = glm::length(tangent);

        // Fallback tangent from neighboring samples if degenerate
        if (tangentLen < 0.0001f)
        {
            if (i + 1 < sampleCount)
            {
                tangent = samples[i + 1] - samples[i];
            }
            else if (i > 0)
            {
                tangent = samples[i] - samples[i - 1];
            }
            else
            {
                tangent = glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }

        tangent = glm::normalize(tangent);
        glm::vec3 right = glm::normalize(glm::cross(tangent, up));

        // V coordinate: tile proportionally along path length
        float vCoord = (totalLength > 0.0001f)
            ? cumulativeDistances[i] / pathWidth
            : 0.0f;

        Vertex leftVert;
        leftVert.position = samples[i] - right * (pathWidth * 0.5f);
        leftVert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        leftVert.color = glm::vec3(1.0f);
        leftVert.texCoord = glm::vec2(0.0f, vCoord);
        leftVert.tangent = glm::vec3(0.0f);
        leftVert.bitangent = glm::vec3(0.0f);
        leftVert.boneIds = glm::ivec4(0);
        leftVert.boneWeights = glm::vec4(0.0f);
        vertices.push_back(leftVert);

        Vertex rightVert;
        rightVert.position = samples[i] + right * (pathWidth * 0.5f);
        rightVert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        rightVert.color = glm::vec3(1.0f);
        rightVert.texCoord = glm::vec2(1.0f, vCoord);
        rightVert.tangent = glm::vec3(0.0f);
        rightVert.bitangent = glm::vec3(0.0f);
        rightVert.boneIds = glm::ivec4(0);
        rightVert.boneWeights = glm::vec4(0.0f);
        vertices.push_back(rightVert);
    }

    // Generate triangle indices (quad strip)
    indices.reserve((sampleCount - 1) * 6);
    for (size_t i = 0; i < sampleCount - 1; ++i)
    {
        uint32_t bl = static_cast<uint32_t>(i * 2);
        uint32_t br = bl + 1;
        uint32_t tl = bl + 2;
        uint32_t tr = bl + 3;

        indices.push_back(bl);
        indices.push_back(br);
        indices.push_back(tl);

        indices.push_back(br);
        indices.push_back(tr);
        indices.push_back(tl);
    }

    // Calculate tangents for normal mapping and upload to GPU
    calculateTangents(vertices, indices);

    auto mesh = std::make_shared<Mesh>();
    mesh->upload(vertices, indices);

    // Create the path entity
    Entity* entity = scene.createEntity("Path");

    auto material = resources.createMaterial(
        "__editor_mat_" + std::to_string(entity->getId()));
    material->setType(MaterialType::PBR);
    material->setAlbedo(glm::vec3(0.6f, 0.55f, 0.45f)); // Earthy path color
    material->setMetallic(0.0f);
    material->setRoughness(0.8f);
    entity->addComponent<MeshRenderer>(mesh, material);

    // Register with undo system
    auto cmd = std::make_unique<CreateEntityCommand>(scene, entity->getId());
    history.execute(std::move(cmd));

    float pathLength = m_spline.getApproxLength(samplesPerSegment);
    Logger::info("PathTool: created path (" + std::to_string(pathLength)
                 + "m long, " + std::to_string(pathWidth)
                 + "m wide, " + std::to_string(m_spline.getPointCount())
                 + " waypoints)");
}

} // namespace Vestige
