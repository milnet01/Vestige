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

class Terrain;

/// @brief Configuration for the first-person controller.
struct ControllerConfig
{
    float moveSpeed = 3.0f;
    float sprintMultiplier = 2.0f;
    float mouseSensitivity = 0.1f;
    float gamepadLookSensitivity = 350.0f;  // Degrees per second
    float gamepadDeadzone = 0.15f;
    float playerHeight = 1.7f;     // Eye height above ground
    float playerRadius = 0.3f;     // Collision radius
    float maxSlopeAngle = 50.0f;   // Maximum walkable slope in degrees
    float terrainDampingUp = 20.0f;   // Damping rate when ascending terrain
    float terrainDampingDown = 12.0f; // Damping rate when descending terrain
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

    /// @brief Sets the terrain for ground collision queries.
    void setTerrain(const Terrain* terrain);

    /// @brief Toggles between walk mode (terrain-grounded) and fly mode.
    void setWalkMode(bool walk);

    /// @brief Returns true if walk mode is active.
    bool isWalkMode() const;

    /// @brief Processes only camera look input (mouse + gamepad right stick).
    /// Used when the physics character controller handles movement.
    void processLookOnly(float deltaTime);

    /// @brief Computes the desired world-space velocity from input without moving the camera.
    /// Returns the velocity vector including sprint. Y component is set from Space/Shift.
    glm::vec3 computeDesiredVelocity(float deltaTime);

private:
    void processKeyboardMovement(float deltaTime, glm::vec3& moveDir);
    void processMouseLook();
    void processGamepad(float deltaTime, glm::vec3& moveDir);
    void applyCollision(glm::vec3& newPosition, const std::vector<AABB>& colliders);
    void applyTerrainCollision(glm::vec3& newPosition, float deltaTime);
    float applyDeadzone(float value) const;

    Camera& m_camera;
    InputManager& m_inputManager;
    ControllerConfig m_config;
    const Terrain* m_terrain = nullptr;
    bool m_isEnabled;
    bool m_walkMode = false;
    bool m_isGamepadSprinting;
    int m_gamepadId;
    float m_smoothedTerrainY = 0.0f; // Smoothed terrain height for damping
    float m_cosMaxSlope = 0.0f;      // Pre-computed cos(maxSlopeAngle)
};

} // namespace Vestige
