// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file path_tool.h
/// @brief Interactive spline-based path/road creation tool.
#pragma once

#include "utils/catmull_rom_spline.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Vestige
{

class Scene;
class ResourceManager;
class CommandHistory;

/// @brief Path presets for quick road/path creation.
enum class PathPreset
{
    NARROW_FOOTPATH,    ///< 1.0m wide, gravel
    WIDE_ROAD,          ///< 4.0m wide, stone
    STONE_WALKWAY       ///< 2.0m wide, stone pavers
};

/// @brief Create paths by clicking waypoints. Generates a spline mesh.
class PathTool
{
public:
    enum class State
    {
        INACTIVE,
        PLACING_POINTS,
        FINISHED
    };

    PathTool() = default;

    void activate();
    void cancel();
    bool processClick(const glm::vec3& hitPoint);
    bool finishPath(Scene& scene, ResourceManager& resources, CommandHistory& history);
    void queueDebugDraw(const glm::vec3& currentHit) const;
    void drawConfigPanel();

    State getState() const { return m_state; }
    bool isActive() const { return m_state == State::PLACING_POINTS; }
    bool showingPanel() const { return m_state != State::INACTIVE; }
    bool finishRequested() const { return m_state == State::FINISHED; }
    size_t getPointCount() const { return m_spline.getPointCount(); }

    // Parameters
    float pathWidth = 2.0f;
    int samplesPerSegment = 10;
    PathPreset preset = PathPreset::STONE_WALKWAY;

private:
    void applyPreset(PathPreset p);
    void generatePathMesh(Scene& scene, ResourceManager& resources,
                          CommandHistory& history);

    State m_state = State::INACTIVE;
    CatmullRomSpline m_spline;
};

} // namespace Vestige
