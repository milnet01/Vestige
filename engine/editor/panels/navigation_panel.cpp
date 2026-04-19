// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file navigation_panel.cpp
/// @brief NavigationPanel implementation.

#include "editor/panels/navigation_panel.h"
#include "core/logger.h"
#include "systems/navigation_system.h"

#include <imgui.h>

namespace Vestige
{

void NavigationPanel::draw(NavigationSystem* navSystem, Scene* scene)
{
    if (!m_open) return;

    if (!ImGui::Begin("Navigation", &m_open))
    {
        ImGui::End();
        return;
    }

    if (!navSystem)
    {
        ImGui::TextDisabled("NavigationSystem not registered.");
        ImGui::End();
        return;
    }

    // -- Status header --
    if (navSystem->hasNavMesh())
    {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Navmesh: built");
        if (m_haveLastBakeStats)
        {
            ImGui::Text("Polygons: %d", m_lastPolyCount);
            ImGui::Text("Last bake: %.1f ms", static_cast<double>(m_lastBuildTimeMs));
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Navmesh: not baked");
    }
    ImGui::Separator();

    // -- Visualisation --
    ImGui::Checkbox("Show polygon overlay", &m_visualize);
    if (m_visualize)
    {
        ImGui::ColorEdit3("Overlay color", &m_overlayColor.x,
                          ImGuiColorEditFlags_NoInputs);
        ImGui::DragFloat("Overlay lift (m)", &m_overlayLift,
                         0.005f, 0.0f, 1.0f, "%.3f");
    }
    ImGui::Separator();

    // -- Bake config --
    if (ImGui::CollapsingHeader("Bake config", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("Cell size (m)",       &m_config.cellSize,
                         0.01f, 0.05f, 2.0f, "%.2f");
        ImGui::DragFloat("Cell height (m)",     &m_config.cellHeight,
                         0.01f, 0.05f, 2.0f, "%.2f");
        ImGui::DragFloat("Agent height (m)",    &m_config.agentHeight,
                         0.05f, 0.5f, 4.0f, "%.2f");
        ImGui::DragFloat("Agent radius (m)",    &m_config.agentRadius,
                         0.01f, 0.05f, 2.0f, "%.2f");
        ImGui::DragFloat("Agent max climb (m)", &m_config.agentMaxClimb,
                         0.01f, 0.0f, 2.0f, "%.2f");
        ImGui::DragFloat("Agent max slope (deg)", &m_config.agentMaxSlope,
                         1.0f, 0.0f, 90.0f, "%.0f");
        ImGui::DragFloat("Region min size",     &m_config.regionMinSize,
                         1.0f, 0.0f, 1024.0f, "%.0f");
        ImGui::DragFloat("Region merge size",   &m_config.regionMergeSize,
                         1.0f, 0.0f, 1024.0f, "%.0f");
        ImGui::DragFloat("Edge max length (m)", &m_config.edgeMaxLen,
                         0.5f, 0.0f, 64.0f, "%.1f");
        ImGui::DragFloat("Edge max error",      &m_config.edgeMaxError,
                         0.05f, 0.1f, 4.0f, "%.2f");
        ImGui::DragInt("Verts per polygon",     &m_config.vertsPerPoly,
                       1, 3, 6);
        ImGui::DragFloat("Detail sample dist",  &m_config.detailSampleDist,
                         0.5f, 0.0f, 32.0f, "%.1f");
        ImGui::DragFloat("Detail sample max err", &m_config.detailSampleMaxError,
                         0.05f, 0.0f, 4.0f, "%.2f");
    }
    ImGui::Separator();

    // -- Bake / clear actions --
    const bool sceneAvailable = scene != nullptr;
    if (!sceneAvailable) ImGui::BeginDisabled();
    if (ImGui::Button("Bake navmesh"))
    {
        const bool ok = navSystem->bakeNavMesh(*scene, m_config);
        if (ok)
        {
            m_haveLastBakeStats = true;
            m_lastPolyCount     = navSystem->getBuilder().getPolyCount();
            m_lastBuildTimeMs   = navSystem->getBuilder().getLastBuildTimeMs();
        }
        else
        {
            Logger::warning("NavigationPanel: bake failed (see prior logs)");
        }
    }
    if (!sceneAvailable) ImGui::EndDisabled();

    ImGui::SameLine();

    const bool canClear = navSystem->hasNavMesh();
    if (!canClear) ImGui::BeginDisabled();
    if (ImGui::Button("Clear"))
    {
        navSystem->clearNavMesh();
        m_haveLastBakeStats = false;
        m_lastPolyCount     = 0;
        m_lastBuildTimeMs   = 0.0f;
    }
    if (!canClear) ImGui::EndDisabled();

    if (!sceneAvailable)
    {
        ImGui::TextDisabled("(load a scene to enable baking)");
    }

    ImGui::End();
}

} // namespace Vestige
