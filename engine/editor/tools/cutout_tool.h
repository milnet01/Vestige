/// @file cutout_tool.h
/// @brief Tool for cutting door/window openings in wall entities.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Vestige
{

class Scene;
class ResourceManager;
class CommandHistory;
class Entity;

/// @brief Cuts rectangular openings (doors, windows) into existing wall meshes.
///
/// Workflow: activate -> select a wall entity -> configure opening params in
/// the ImGui dialog -> apply to regenerate the wall mesh with the opening.
class CutoutTool
{
public:
    enum class State
    {
        INACTIVE,       ///< Tool is off.
        SELECT_WALL,    ///< Waiting for the user to select a wall entity.
        CONFIGURE       ///< Wall selected, showing the configuration dialog.
    };

    /// @brief Type of opening to cut.
    enum class OpeningType
    {
        DOOR,       ///< Floor-level opening (sillHeight = 0).
        WINDOW      ///< Elevated opening (sillHeight > 0).
    };

    CutoutTool() = default;

    /// @brief Activates the tool (enters SELECT_WALL state).
    void activate();

    /// @brief Cancels the current operation and deactivates the tool.
    void cancel();

    /// @brief Selects a wall entity for cutting.
    /// @param entityId ID of the entity to select.
    /// @param scene Scene reference for entity lookup.
    void selectWall(uint32_t entityId, Scene& scene);

    /// @brief Draws the opening configuration ImGui dialog.
    /// @param scene Scene reference for entity lookup.
    /// @param resources Resource manager for material access.
    /// @param history Command history for undo support.
    void drawConfigDialog(Scene& scene, ResourceManager& resources,
                          CommandHistory& history);

    /// @brief Gets the current state.
    State getState() const { return m_state; }

    /// @brief Returns true if the tool is actively waiting for input.
    bool isActive() const { return m_state != State::INACTIVE; }

    /// @brief Returns true if the ImGui dialog should be displayed.
    bool showingDialog() const { return m_state == State::CONFIGURE; }

    // Opening parameters
    OpeningType openingType = OpeningType::DOOR;    ///< Door or window.
    float openingWidth = 0.9f;      ///< Opening width in meters.
    float openingHeight = 2.1f;     ///< Opening height in meters.
    float sillHeight = 0.0f;        ///< Height from wall bottom to opening bottom (0 for doors, ~0.9m for windows).
    float xPosition = 0.5f;         ///< Normalized position along wall width (0 = left edge, 1 = right edge).

private:
    State m_state = State::INACTIVE;
    uint32_t m_selectedWallId = 0;
};

} // namespace Vestige
