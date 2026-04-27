// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file first_person_controller.cpp
/// @brief First-person controller implementation.
#include "core/first_person_controller.h"
#include "core/logger.h"
#include "environment/terrain.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace Vestige
{

FirstPersonController::FirstPersonController(Camera& camera, InputManager& inputManager,
                                             const ControllerConfig& config)
    : m_camera(camera)
    , m_inputManager(inputManager)
    , m_config(config)
    , m_isEnabled(true)
    , m_isGamepadSprinting(false)
    , m_gamepadId(-1)
{
    m_camera.setSensitivity(config.mouseSensitivity);
    m_cosMaxSlope = std::cos(glm::radians(m_config.maxSlopeAngle));

    // Check for connected gamepads
    for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++)
    {
        if (glfwJoystickIsGamepad(i))
        {
            m_gamepadId = i;
            const char* name = glfwGetGamepadName(i);
            Logger::info("Gamepad detected: " + std::string(name ? name : "Unknown"));
            break;
        }
    }
}

void FirstPersonController::update(float deltaTime, const std::vector<AABB>& colliders)
{
    if (!m_isEnabled)
    {
        return;
    }

    // Check for gamepad connection changes — rate-limited per Pe7.
    if (m_gamepadId < 0)
    {
        if (tickJoystickScanTimer(deltaTime))
        {
            for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++)
            {
                if (glfwJoystickIsGamepad(i))
                {
                    m_gamepadId = i;
                    const char* name = glfwGetGamepadName(i);
                    Logger::info("Gamepad connected: " + std::string(name ? name : "Unknown"));
                    break;
                }
            }
        }
    }
    else if (!glfwJoystickPresent(m_gamepadId))
    {
        Logger::info("Gamepad disconnected");
        m_gamepadId = -1;
    }

    glm::vec3 moveDir(0.0f);

    // Gather input from all sources
    processKeyboardMovement(deltaTime, moveDir);
    processMouseLook();
    processGamepad(deltaTime, moveDir);

    // Apply movement
    if (glm::length(moveDir) > 0.0f || m_walkMode)
    {
        // Sprint check
        float speed = m_config.moveSpeed;
        if (m_inputManager.isKeyDown(GLFW_KEY_LEFT_CONTROL) || m_isGamepadSprinting)
        {
            speed *= m_config.sprintMultiplier;
        }

        glm::vec3 currentPos = m_camera.getPosition();
        glm::vec3 newPosition = currentPos;

        if (glm::length(moveDir) > 0.0f)
        {
            moveDir = glm::normalize(moveDir);
            glm::vec3 front = m_camera.getFront();
            glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));

            // Movement is along the ground plane (Y from up/down keys only in fly mode)
            glm::vec3 flatFront = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
            glm::vec3 flatRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

            newPosition += flatFront * moveDir.z * speed * deltaTime;
            newPosition += flatRight * moveDir.x * speed * deltaTime;

            if (!m_walkMode)
            {
                // Fly mode: vertical movement from Space/Shift
                newPosition.y += moveDir.y * speed * deltaTime;
            }
        }

        // Terrain collision in walk mode
        if (m_walkMode && m_terrain)
        {
            applyTerrainCollision(newPosition, deltaTime);
        }
        else
        {
            // Fly mode: basic ground clamp at player height
            newPosition.y = std::max(newPosition.y, m_config.playerHeight);
        }

        // AABB collision detection
        if (!colliders.empty())
        {
            applyCollision(newPosition, colliders);
        }

        // Update camera position directly
        m_camera.setPosition(newPosition);
    }
}

void FirstPersonController::processKeyboardMovement(float /*deltaTime*/, glm::vec3& moveDir)
{
    if (m_inputManager.isKeyDown(GLFW_KEY_W))
    {
        moveDir.z += 1.0f;
    }
    if (m_inputManager.isKeyDown(GLFW_KEY_S))
    {
        moveDir.z -= 1.0f;
    }
    if (m_inputManager.isKeyDown(GLFW_KEY_D))
    {
        moveDir.x += 1.0f;
    }
    if (m_inputManager.isKeyDown(GLFW_KEY_A))
    {
        moveDir.x -= 1.0f;
    }
    if (m_inputManager.isKeyDown(GLFW_KEY_SPACE))
    {
        moveDir.y += 1.0f;
    }
    if (m_inputManager.isKeyDown(GLFW_KEY_LEFT_SHIFT))
    {
        moveDir.y -= 1.0f;
    }
}

void FirstPersonController::processMouseLook()
{
    glm::vec2 mouseDelta = m_inputManager.getMouseDelta();
    if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)
    {
        m_camera.rotate(mouseDelta.x, -mouseDelta.y);
    }
}

void FirstPersonController::processGamepad(float deltaTime, glm::vec3& moveDir)
{
    if (m_gamepadId < 0)
    {
        return;
    }

    GLFWgamepadstate state;
    if (!glfwGetGamepadState(m_gamepadId, &state))
    {
        return;
    }

    // Left stick — movement
    float leftX = applyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]);
    float leftY = applyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);

    if (leftX != 0.0f || leftY != 0.0f)
    {
        moveDir.x += leftX;
        moveDir.z -= leftY;  // GLFW Y axis is inverted
    }

    // Right stick — camera look
    float rightX = applyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]);
    float rightY = applyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);

    if (rightX != 0.0f || rightY != 0.0f)
    {
        float lookSpeed = m_config.gamepadLookSensitivity * deltaTime;
        m_camera.rotate(rightX * lookSpeed, -rightY * lookSpeed);
    }

    // Triggers — up/down
    float leftTrigger = (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
    float rightTrigger = (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
    moveDir.y += rightTrigger - leftTrigger;

    // Sprint with left bumper
    m_isGamepadSprinting = state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] != 0;
}

void FirstPersonController::applyTerrainCollision(glm::vec3& newPosition, float deltaTime)
{
    if (!m_terrain || !m_terrain->isInitialized())
    {
        return;
    }

    float terrainH = m_terrain->getHeight(newPosition.x, newPosition.z);
    float targetY = terrainH + m_config.playerHeight;

    // Slope limiting: reject movement onto slopes steeper than maxSlopeAngle
    glm::vec3 terrainNormal = m_terrain->getNormal(newPosition.x, newPosition.z);
    if (terrainNormal.y < m_cosMaxSlope && targetY > m_smoothedTerrainY)
    {
        // Slope too steep and we'd be going uphill — revert XZ to previous position
        glm::vec3 camPos = m_camera.getPosition();
        newPosition.x = camPos.x;
        newPosition.z = camPos.z;

        // Recompute height at the reverted position
        terrainH = m_terrain->getHeight(newPosition.x, newPosition.z);
        targetY = terrainH + m_config.playerHeight;
    }

    // Asymmetric exponential damping: fast up, smooth down
    float lambda = (targetY > m_smoothedTerrainY)
        ? m_config.terrainDampingUp    // Ascending — respond quickly
        : m_config.terrainDampingDown; // Descending — ease down gently

    m_smoothedTerrainY = glm::mix(m_smoothedTerrainY, targetY,
                                   1.0f - std::exp(-lambda * deltaTime));

    // Hard floor: never go below terrain surface
    m_smoothedTerrainY = std::max(m_smoothedTerrainY, targetY);

    newPosition.y = m_smoothedTerrainY;
}

void FirstPersonController::applyCollision(glm::vec3& newPosition, const std::vector<AABB>& colliders)
{
    // Player AABB extends from feet to head. The camera is at eye height,
    // so the body center is half the player height below the camera.
    glm::vec3 bodyCenter(newPosition.x, newPosition.y - m_config.playerHeight * 0.5f, newPosition.z);
    AABB playerBounds = AABB::fromCenterSize(
        bodyCenter,
        glm::vec3(m_config.playerRadius * 2.0f, m_config.playerHeight, m_config.playerRadius * 2.0f)
    );

    for (const auto& collider : colliders)
    {
        if (playerBounds.intersects(collider))
        {
            glm::vec3 pushOut = playerBounds.getMinPushOut(collider);
            newPosition += pushOut;

            // Recompute player bounds after push
            bodyCenter = glm::vec3(newPosition.x, newPosition.y - m_config.playerHeight * 0.5f, newPosition.z);
            playerBounds = AABB::fromCenterSize(
                bodyCenter,
                glm::vec3(m_config.playerRadius * 2.0f, m_config.playerHeight, m_config.playerRadius * 2.0f)
            );
        }
    }
}

float FirstPersonController::applyDeadzone(float value) const
{
    // AUDIT M28: sanitize at the input boundary — a faulty HID report or
    // driver bug can produce NaN or out-of-range axis values that would
    // otherwise propagate into camera rotation and movement. Finite check
    // first (NaN compares false with every clamp bound), then clamp to the
    // GLFW-documented [-1, 1] envelope.
    if (!std::isfinite(value))
    {
        return 0.0f;
    }
    value = std::clamp(value, -1.0f, 1.0f);

    if (std::abs(value) < m_config.gamepadDeadzone)
    {
        return 0.0f;
    }
    // Remap remaining range to 0-1
    float sign = (value > 0.0f) ? 1.0f : -1.0f;
    return sign * (std::abs(value) - m_config.gamepadDeadzone) / (1.0f - m_config.gamepadDeadzone);
}

void FirstPersonController::setEnabled(bool isEnabled)
{
    m_isEnabled = isEnabled;
}

bool FirstPersonController::isEnabled() const
{
    return m_isEnabled;
}

ControllerConfig& FirstPersonController::getConfig()
{
    return m_config;
}

AABB FirstPersonController::getPlayerBounds() const
{
    glm::vec3 camPos = m_camera.getPosition();
    glm::vec3 bodyCenter(camPos.x, camPos.y - m_config.playerHeight * 0.5f, camPos.z);
    return AABB::fromCenterSize(
        bodyCenter,
        glm::vec3(m_config.playerRadius * 2.0f, m_config.playerHeight, m_config.playerRadius * 2.0f)
    );
}

void FirstPersonController::setTerrain(const Terrain* terrain)
{
    m_terrain = terrain;
}

void FirstPersonController::setWalkMode(bool walk)
{
    if (walk && !m_walkMode)
    {
        // Initialize smoothed height from current camera position
        m_smoothedTerrainY = m_camera.getPosition().y;
    }
    m_walkMode = walk;
}

bool FirstPersonController::isWalkMode() const
{
    return m_walkMode;
}

void FirstPersonController::processLookOnly(float deltaTime)
{
    if (!m_isEnabled)
    {
        return;
    }

    // Check for gamepad connection changes (same as in update()) —
    // rate-limited per Pe7.
    if (m_gamepadId < 0)
    {
        if (tickJoystickScanTimer(deltaTime))
        {
            for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++)
            {
                if (glfwJoystickIsGamepad(i))
                {
                    m_gamepadId = i;
                    const char* name = glfwGetGamepadName(i);
                    Logger::info("Gamepad connected: " + std::string(name ? name : "Unknown"));
                    break;
                }
            }
        }
    }
    else if (!glfwJoystickPresent(m_gamepadId))
    {
        Logger::info("Gamepad disconnected");
        m_gamepadId = -1;
    }

    // Mouse look
    processMouseLook();

    // Gamepad right-stick look only
    if (m_gamepadId >= 0)
    {
        GLFWgamepadstate state;
        if (glfwGetGamepadState(m_gamepadId, &state))
        {
            float rightX = applyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]);
            float rightY = applyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
            if (rightX != 0.0f || rightY != 0.0f)
            {
                float lookSpeed = m_config.gamepadLookSensitivity * deltaTime;
                m_camera.rotate(rightX * lookSpeed, -rightY * lookSpeed);
            }
        }
    }
}

glm::vec3 FirstPersonController::computeDesiredVelocity(float /*deltaTime*/)
{
    if (!m_isEnabled)
    {
        return glm::vec3(0.0f);
    }

    glm::vec3 moveDir(0.0f);

    // Keyboard movement
    if (m_inputManager.isKeyDown(GLFW_KEY_W))    moveDir.z += 1.0f;
    if (m_inputManager.isKeyDown(GLFW_KEY_S))    moveDir.z -= 1.0f;
    if (m_inputManager.isKeyDown(GLFW_KEY_D))    moveDir.x += 1.0f;
    if (m_inputManager.isKeyDown(GLFW_KEY_A))    moveDir.x -= 1.0f;
    if (m_inputManager.isKeyDown(GLFW_KEY_SPACE))       moveDir.y += 1.0f;
    if (m_inputManager.isKeyDown(GLFW_KEY_LEFT_SHIFT))  moveDir.y -= 1.0f;

    // Gamepad left-stick movement + triggers
    if (m_gamepadId >= 0)
    {
        GLFWgamepadstate state;
        if (glfwGetGamepadState(m_gamepadId, &state))
        {
            float leftX = applyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]);
            float leftY = applyDeadzone(state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
            if (leftX != 0.0f || leftY != 0.0f)
            {
                moveDir.x += leftX;
                moveDir.z -= leftY;
            }

            float leftTrigger = (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
            float rightTrigger = (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
            moveDir.y += rightTrigger - leftTrigger;

            m_isGamepadSprinting = state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] != 0;
        }
    }

    if (glm::length(moveDir) < 0.001f)
    {
        return glm::vec3(0.0f);
    }

    moveDir = glm::normalize(moveDir);

    // Sprint speed
    float speed = m_config.moveSpeed;
    if (m_inputManager.isKeyDown(GLFW_KEY_LEFT_CONTROL) || m_isGamepadSprinting)
    {
        speed *= m_config.sprintMultiplier;
    }

    // Project onto world-space directions using camera orientation
    glm::vec3 front = m_camera.getFront();
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 flatFront = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
    glm::vec3 flatRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

    glm::vec3 velocity = flatFront * moveDir.z * speed
                       + flatRight * moveDir.x * speed
                       + glm::vec3(0.0f, moveDir.y * speed, 0.0f);

    return velocity;
}

bool FirstPersonController::tickJoystickScanTimer(float deltaTime)
{
    m_secondsUntilNextJoystickScan -= deltaTime;
    if (m_secondsUntilNextJoystickScan > 0.0f)
    {
        return false;
    }
    m_secondsUntilNextJoystickScan = kJoystickScanInterval;
    return true;
}

} // namespace Vestige
