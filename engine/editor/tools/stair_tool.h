/// @file stair_tool.h
/// @brief Stair/ramp creation tool with configurable parameters.
#pragma once

#include "utils/procedural_mesh.h"

#include <glm/glm.hpp>

namespace Vestige
{

class Scene;
class ResourceManager;
class CommandHistory;

/// @brief Configure stair parameters in an ImGui panel, then click to place.
///
/// Supports straight and spiral staircases. Workflow: activate -> configure
/// type/dimensions in panel -> click "Place" -> click viewport to position.
class StairTool
{
public:
    enum class State
    {
        INACTIVE,   ///< Tool is off.
        CONFIGURE,  ///< Showing the ImGui configuration panel.
        PLACING     ///< Configuration done, waiting for a click to place the stairs.
    };

    StairTool() = default;

    /// @brief Activates the tool (enters CONFIGURE state).
    void activate();

    /// @brief Cancels the current operation and deactivates the tool.
    void cancel();

    /// @brief Processes a viewport click to place the configured stairs.
    /// @param hitPoint World-space position of the click.
    /// @param scene Scene to create the stair entity in.
    /// @param resources Resource manager for material creation.
    /// @param history Command history for undo support.
    /// @return True if the tool consumed the click.
    bool processClick(const glm::vec3& hitPoint, Scene& scene,
                      ResourceManager& resources, CommandHistory& history);

    /// @brief Draws the ImGui configuration panel.
    void drawConfigPanel();

    /// @brief Gets the current state.
    State getState() const { return m_state; }

    /// @brief Returns true if the tool is actively waiting for input.
    bool isActive() const { return m_state != State::INACTIVE; }

    /// @brief Returns true if the ImGui panel should be displayed.
    bool showingPanel() const { return m_state == State::CONFIGURE; }

    // Parameters
    StairType stairType = StairType::STRAIGHT;  ///< Straight or spiral.
    float totalHeight = 3.0f;       ///< Total height of the staircase in meters.
    float stepHeight = 0.18f;       ///< Individual step rise in meters.
    float stepDepth = 0.28f;        ///< Individual step tread depth in meters.
    float width = 1.0f;             ///< Stair width in meters (straight only).
    // Spiral-specific parameters
    float innerRadius = 0.3f;       ///< Inner radius of spiral staircase.
    float outerRadius = 1.5f;       ///< Outer radius of spiral staircase.
    float totalAngle = 360.0f;      ///< Total rotation in degrees (spiral only).

private:
    State m_state = State::INACTIVE;
};

} // namespace Vestige
