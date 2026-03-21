/// @file editor_camera.h
/// @brief Orbit/pan/zoom camera for the scene editor.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

class Camera;

/// @brief Turntable camera for scene editing — orbits around a focus point.
///
/// Controls (when viewport is hovered):
/// - Alt + LMB drag: orbit around focus point
/// - MMB drag: pan the view plane
/// - Scroll wheel: zoom (change distance to focus)
/// - Numpad 1/3/7: front/right/top preset views
/// - F key: focus on selected entity (or scene center)
class EditorCamera
{
public:
    EditorCamera();

    /// @brief Updates smooth transitions. Call each frame.
    /// @param deltaTime Time since last frame in seconds.
    void update(float deltaTime);

    /// @brief Reads ImGui IO state for orbit/pan/zoom input.
    /// @param viewportHovered True if the mouse is over the viewport panel.
    void processInput(bool viewportHovered);

    /// @brief Applies the editor camera state to a Camera for rendering.
    /// Sets the camera's position, yaw, and pitch to match the orbit view.
    /// @param camera The camera to update.
    void applyToCamera(Camera& camera) const;

    /// @brief Smoothly animates the camera to look at a world-space position.
    /// @param target World position to focus on.
    /// @param distance Desired distance. 0 = keep current distance.
    void focusOn(const glm::vec3& target, float distance = 0.0f);

    /// @brief Front view preset (Numpad 1): camera at +Z looking at -Z.
    void setFrontView();

    /// @brief Right view preset (Numpad 3): camera at +X looking at -X.
    void setRightView();

    /// @brief Top view preset (Numpad 7): camera above looking down.
    void setTopView();

    glm::vec3 getPosition() const;
    glm::vec3 getFocusPoint() const;
    float getDistance() const;

    /// @brief Syncs the editor camera from an existing camera's position/orientation.
    /// Used when switching back from play mode so the orbit camera picks up
    /// where the FPS camera left off instead of snapping to its old state.
    /// @param camera The camera to sync from.
    void syncFromCamera(const Camera& camera);

private:
    /// @brief Recomputes eye position from orbit parameters.
    void computePosition();

    // Current state (smoothly interpolated toward targets)
    glm::vec3 m_focusPoint;
    float m_distance;
    float m_yaw;    ///< Horizontal orbit angle in degrees.
    float m_pitch;  ///< Vertical orbit angle in degrees.

    // Target state for smooth transitions
    glm::vec3 m_targetFocusPoint;
    float m_targetDistance;
    float m_targetYaw;
    float m_targetPitch;

    // Computed from orbit state
    glm::vec3 m_position;

    // Tuning
    float m_orbitSensitivity;
    float m_panSensitivity;
    float m_zoomSensitivity;
    float m_smoothSpeed;
    float m_minDistance;
    float m_maxDistance;
    float m_minPitch;
    float m_maxPitch;
};

} // namespace Vestige
