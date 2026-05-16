// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file navigation_panel.h
/// @brief ImGui panel for baking the navmesh and visualizing the result.
#pragma once

#include "editor/panels/i_panel.h"
#include "navigation/nav_mesh_config.h"

#include <glm/glm.hpp>

namespace Vestige
{

class NavigationSystem;
class Scene;

/// @brief Editor panel for navmesh bake controls + polygon visualisation.
///
/// Drives `NavigationSystem` from the editor: exposes Recast build params,
/// fires `bakeNavMesh()` on demand, reports last-bake stats, and toggles
/// a debug overlay drawn by the engine's render pass.
class NavigationPanel : public IPanel
{
public:
    const char* displayName() const override { return "Navigation"; }

    /// @brief Draws the panel inside its own ImGui window.
    /// @param navSystem Live navigation system. May be null — panel disables in that case.
    /// @param scene    Active scene to bake from. May be null.
    void draw(NavigationSystem* navSystem, Scene* scene);

    /// @brief Whether the panel is currently visible.
    bool isOpen() const override { return m_open; }

    /// @brief Toggle panel visibility.
    void setOpen(bool open) override { m_open = open; }

    /// @brief Whether the navmesh polygon overlay should be drawn this frame.
    bool isVisualizationEnabled() const { return m_visualize; }

    /// @brief Color used for navmesh polygon edges.
    const glm::vec3& getOverlayColor() const { return m_overlayColor; }

    /// @brief Y-offset added to drawn polygon edges to avoid z-fighting with ground.
    float getOverlayLift() const { return m_overlayLift; }

private:
    bool m_open = false;
    bool m_visualize = false;
    NavMeshBuildConfig m_config;

    // Last-bake telemetry (populated after a successful bake).
    bool m_haveLastBakeStats = false;
    int m_lastPolyCount = 0;
    float m_lastBuildTimeMs = 0.0f;

    // Overlay rendering parameters.
    glm::vec3 m_overlayColor = glm::vec3(0.20f, 0.85f, 1.00f);  ///< Cyan
    float m_overlayLift = 0.05f;                                  ///< Meters above polygon Y
};

} // namespace Vestige
