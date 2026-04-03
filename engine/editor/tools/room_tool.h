/// @file room_tool.h
/// @brief Room creation tool -- enter dimensions or click corners.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

class Entity;
class Scene;
class ResourceManager;
class CommandHistory;

/// @brief Creates rectangular rooms (4 walls + optional floor/ceiling).
///
/// Two modes: dimension-input (ImGui dialog) or click-to-place (click a
/// corner in the viewport). Both call the same internal createRoom() method.
class RoomTool
{
public:
    enum class State
    {
        INACTIVE,           ///< Tool is off.
        DIMENSION_INPUT,    ///< Showing the ImGui dimension dialog.
        WAITING_CORNER      ///< Waiting for a click to place the room.
    };

    RoomTool() = default;

    /// @brief Activates dimension-input mode (opens ImGui dialog).
    void activateDimensionMode();

    /// @brief Activates click-to-place mode (click a corner in the viewport).
    void activateClickMode();

    /// @brief Cancels the current operation and deactivates the tool.
    void cancel();

    /// @brief Processes a viewport click to place a room.
    /// @param hitPoint World-space position of the click.
    /// @param scene Scene to create the room in.
    /// @param resources Resource manager for material creation.
    /// @param history Command history for undo support.
    /// @return True if the tool consumed the click.
    bool processClick(const glm::vec3& hitPoint, Scene& scene,
                      ResourceManager& resources, CommandHistory& history);

    /// @brief Draws the dimension-input ImGui dialog.
    /// @param scene Scene to create the room in (used when "Create" is clicked).
    /// @param resources Resource manager for material creation.
    /// @param history Command history for undo support.
    void drawDimensionDialog(Scene& scene, ResourceManager& resources,
                             CommandHistory& history);

    /// @brief Draws debug preview (currently no preview for room tool).
    void queueDebugDraw() const;

    /// @brief Gets the current state.
    State getState() const { return m_state; }

    /// @brief Returns true if the tool is actively waiting for input.
    bool isActive() const { return m_state != State::INACTIVE; }

    /// @brief Returns true if the ImGui dialog should be displayed.
    bool showingDialog() const { return m_state == State::DIMENSION_INPUT; }

    // Parameters
    float roomWidth = 4.0f;         ///< Room width in meters (X axis).
    float roomDepth = 4.0f;         ///< Room depth in meters (Z axis).
    float roomHeight = 3.0f;        ///< Wall height in meters.
    float wallThickness = 0.2f;     ///< Wall thickness in meters.
    bool includeFloor = true;       ///< Whether to generate a floor slab.
    bool includeCeiling = false;    ///< Whether to generate a ceiling slab.

private:
    /// @brief Internal helper: creates the room entity hierarchy at the given position.
    Entity* createRoom(const glm::vec3& position, Scene& scene,
                       ResourceManager& resources, CommandHistory& history);

    State m_state = State::INACTIVE;
    glm::vec3 m_clickPosition{0.0f};
};

} // namespace Vestige
