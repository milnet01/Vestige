// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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

/// @brief The action ids `FirstPersonController` polls for movement.
///
/// `Engine::initialize` registers exactly these on the `InputActionMap`
/// (3D_E-0628b). Both sides name the same constants deliberately: a
/// controller polling an id nobody registered gets a permanent `false`,
/// so the key simply stops working with no error anywhere. Sharing the
/// spelling makes that drift impossible instead of merely tested-for.
namespace MovementActions
{
inline constexpr const char* Forward  = "MoveForward";
inline constexpr const char* Backward = "MoveBackward";
inline constexpr const char* Left     = "MoveLeft";
inline constexpr const char* Right    = "MoveRight";
inline constexpr const char* Up       = "MoveUp";
inline constexpr const char* Down     = "MoveDown";
inline constexpr const char* Sprint   = "Sprint";
} // namespace MovementActions

/// @brief Configuration for the first-person controller.
struct ControllerConfig
{
    float moveSpeed = 3.0f;
    float sprintMultiplier = 2.0f;
    float mouseSensitivity = 0.1f;
    bool  invertY = false;                  // Invert vertical look (mouse + right stick)
    float gamepadLookSensitivity = 350.0f;  // Degrees per second
    // Per-stick deadzones. Separate because the sticks do different jobs:
    // the left one walks you into a wall, the right one swings the camera,
    // so the tolerable amount of drift differs. Defaults match
    // `ControlsSettings` (3D_E-0628a), which is what feeds them at runtime.
    float gamepadDeadzoneLeft = 0.15f;
    float gamepadDeadzoneRight = 0.10f;
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

    /// @brief Sets look sensitivity, forwarding to the camera (3D_E-0628a).
    ///
    /// The camera holds its own copy, set once at construction. Writing
    /// `getConfig().mouseSensitivity` alone therefore changed nothing —
    /// which is what left the Settings slider inert. Use this instead.
    void setMouseSensitivity(float sensitivity);

    /// @brief Inverts vertical look for mouse and gamepad right stick.
    void setInvertY(bool invert);

    /// @brief Sets the per-stick deadzones (left = move, right = look).
    void setGamepadDeadzones(float left, float right);

    /// @brief Points the controller at the action map that names its
    ///        movement verbs (3D_E-0628b).
    ///
    /// Movement is always read through `MovementActions::*` — never by
    /// polling a raw key. `engine/input`'s spec § 12 forbids a raw poll
    /// reachable from gameplay code, and a fallback that polled keys
    /// when no map was set would reintroduce exactly that, one call
    /// site instead of fourteen.
    ///
    /// So the controller builds its own map of the default bindings at
    /// construction and uses that until this is called. An embedder
    /// with no `Engine` therefore still gets working WASD, through the
    /// binding layer rather than around it. Passing `nullptr` restores
    /// the built-in map.
    ///
    /// Does not take ownership. A supplied map must outlive the
    /// controller.
    void setActionMap(const InputActionMap* map);

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
    float applyDeadzone(float value, float deadzone) const;

    /// @brief True if a movement verb is currently active.
    bool movementActionDown(const char* actionId) const;

    /// @brief Registers the default movement bindings on the built-in map.
    void buildDefaultActionMap();

    Camera& m_camera;
    InputManager& m_inputManager;
    ControllerConfig m_config;
    /// Default bindings, used until `setActionMap` supplies the engine's.
    InputActionMap m_defaultActionMap;
    /// Never null — points at `m_defaultActionMap` or a supplied map.
    const InputActionMap* m_actionMap = nullptr;
    const Terrain* m_terrain = nullptr;
    bool m_isEnabled;
    bool m_walkMode = false;
    bool m_isGamepadSprinting;
    int m_gamepadId;
    float m_smoothedTerrainY = 0.0f; // Smoothed terrain height for damping
    float m_cosMaxSlope = 0.0f;      // Pre-computed cos(maxSlopeAngle)

    // AUDIT Pe7 — joystick-presence probe rate-limit. Pre-Pe7 we ran
    // glfwJoystickIsGamepad over all 16 slots every frame (~960 probes/sec
    // at 60 FPS) to detect hot-plug, even on machines without a gamepad.
    // Now we only re-scan once per second when no gamepad is connected;
    // the connected-gamepad poll is unaffected because that's a single
    // glfwJoystickPresent() call per frame.
    float m_secondsUntilNextJoystickScan = 0.0f;
    static constexpr float kJoystickScanInterval = 1.0f;
    /// @brief Returns true if it's time to re-scan for hot-plugged gamepads,
    /// resetting the timer. Stays at most one branch in the hot path when
    /// a gamepad is already connected.
    bool tickJoystickScanTimer(float deltaTime);
};

} // namespace Vestige
