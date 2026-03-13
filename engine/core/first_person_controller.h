/// @file first_person_controller.h
/// @brief First-person character controller with keyboard, mouse, and gamepad input.
#pragma once

#include "core/input_manager.h"
#include "renderer/camera.h"
#include "utils/aabb.h"

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief Configuration for the first-person controller.
struct ControllerConfig
{
    float moveSpeed = 3.0f;
    float sprintMultiplier = 2.0f;
    float mouseSensitivity = 0.1f;
    float gamepadLookSensitivity = 120.0f;  // Degrees per second
    float gamepadDeadzone = 0.15f;
    float playerHeight = 1.7f;     // Eye height above ground
    float playerRadius = 0.3f;     // Collision radius
};

/// @brief First-person controller — handles movement, looking, and collision.
class FirstPersonController
{
public:
    /// @brief Creates the controller.
    /// @param camera The camera to control.
    /// @param inputManager Input manager for reading input state.
    /// @param config Controller settings.
    FirstPersonController(Camera& camera, InputManager& inputManager,
                          const ControllerConfig& config = ControllerConfig());

    /// @brief Updates movement and camera based on input.
    /// @param deltaTime Time elapsed since last frame.
    /// @param colliders World-space AABBs to collide against.
    void update(float deltaTime, const std::vector<AABB>& colliders = {});

    /// @brief Enables or disables input processing.
    void setEnabled(bool isEnabled);

    /// @brief Checks if input processing is enabled.
    bool isEnabled() const;

    /// @brief Gets the controller configuration (for runtime adjustment).
    ControllerConfig& getConfig();

    /// @brief Gets the player's collision AABB in world space.
    AABB getPlayerBounds() const;

private:
    void processKeyboardMovement(float deltaTime, glm::vec3& moveDir);
    void processMouseLook();
    void processGamepad(float deltaTime, glm::vec3& moveDir);
    void applyCollision(glm::vec3& newPosition, const std::vector<AABB>& colliders);
    float applyDeadzone(float value) const;

    Camera& m_camera;
    InputManager& m_inputManager;
    ControllerConfig m_config;
    bool m_isEnabled;
    bool m_isGamepadSprinting;
    int m_gamepadId;
};

} // namespace Vestige
