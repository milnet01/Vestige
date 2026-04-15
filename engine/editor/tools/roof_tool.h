// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file roof_tool.h
/// @brief Roof generation tool -- configure and place roof entities.
#pragma once

#include "utils/procedural_mesh.h"

#include <glm/glm.hpp>

namespace Vestige
{

class Scene;
class ResourceManager;
class CommandHistory;

/// @brief Configure roof parameters in an ImGui panel, then click to place.
///
/// Workflow: activate -> configure type/dimensions in panel -> click "Place"
/// -> click viewport to position the roof entity.
class RoofTool
{
public:
    enum class State
    {
        INACTIVE,   ///< Tool is off.
        CONFIGURE,  ///< Showing the ImGui configuration panel.
        PLACING     ///< Configuration done, waiting for a click to place the roof.
    };

    RoofTool() = default;

    /// @brief Activates the tool (enters CONFIGURE state).
    void activate();

    /// @brief Cancels the current operation and deactivates the tool.
    void cancel();

    /// @brief Processes a viewport click to place the configured roof.
    /// @param hitPoint World-space position of the click.
    /// @param scene Scene to create the roof entity in.
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
    RoofType roofType = RoofType::GABLE;    ///< Roof shape (FLAT, GABLE, SHED).
    float roofWidth = 4.0f;     ///< Roof span in meters (X axis).
    float roofDepth = 4.0f;     ///< Roof length in meters (Z axis).
    float peakHeight = 1.5f;    ///< Height of the ridge above the eave line.
    float overhang = 0.3f;      ///< Eave overhang beyond the wall line.

private:
    State m_state = State::INACTIVE;
};

} // namespace Vestige
