// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file wall_tool.h
/// @brief Interactive two-click wall placement tool.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

class Scene;
class ResourceManager;
class CommandHistory;

/// @brief Place walls by clicking two grid points. Configurable height and thickness.
///
/// Workflow: activate -> click start point -> click end point -> wall created.
/// The tool stays active after placing, ready for the next wall segment.
class WallTool
{
public:
    enum class State
    {
        INACTIVE,       ///< Tool is off.
        WAITING_START,  ///< Waiting for the first click (wall start).
        WAITING_END     ///< First point set, waiting for second click (wall end).
    };

    WallTool() = default;

    /// @brief Activates the tool (enters WAITING_START state).
    void activate();

    /// @brief Cancels the current placement and deactivates the tool.
    void cancel();

    /// @brief Processes a viewport click to define wall endpoints.
    /// @param hitPoint World-space position of the click.
    /// @param scene Scene to create the wall entity in.
    /// @param resources Resource manager for material creation.
    /// @param history Command history for undo support.
    /// @return True if the tool consumed the click.
    bool processClick(const glm::vec3& hitPoint, Scene& scene,
                      ResourceManager& resources, CommandHistory& history);

    /// @brief Draws a preview line from the start point to the cursor.
    /// @param currentHit Current cursor world-space hit position.
    void queueDebugDraw(const glm::vec3& currentHit) const;

    /// @brief Gets the current state.
    State getState() const { return m_state; }

    /// @brief Returns true if the tool is actively waiting for input.
    bool isActive() const { return m_state != State::INACTIVE; }

    // Configurable parameters
    float height = 3.0f;        ///< Wall height in meters.
    float thickness = 0.2f;     ///< Wall thickness in meters.

private:
    State m_state = State::INACTIVE;
    glm::vec3 m_startPoint{0.0f};
};

} // namespace Vestige
