/// @file first_person_controller.cpp
/// @brief First-person controller implementation.
#include "core/first_person_controller.h"
#include "core/logger.h"

#include <GLFW/glfw3.h>

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

    // Check for gamepad connection changes
    if (m_gamepadId < 0)
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
    if (glm::length(moveDir) > 0.0f)
    {
        // Sprint check
        float speed = m_config.moveSpeed;
        if (m_inputManager.isKeyDown(GLFW_KEY_LEFT_CONTROL) || m_isGamepadSprinting)
        {
            speed *= m_config.sprintMultiplier;
        }

        moveDir = glm::normalize(moveDir);
        glm::vec3 currentPos = m_camera.getPosition();
        glm::vec3 front = m_camera.getFront();
        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));

        // Movement is along the ground plane (Y from up/down keys only)
        glm::vec3 flatFront = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
        glm::vec3 flatRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

        glm::vec3 newPosition = currentPos;
        newPosition += flatFront * moveDir.z * speed * deltaTime;
        newPosition += flatRight * moveDir.x * speed * deltaTime;
        newPosition.y += moveDir.y * speed * deltaTime;

        // Ground clamping — keep at player height
        newPosition.y = std::max(newPosition.y, m_config.playerHeight);

        // Collision detection
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

void FirstPersonController::applyCollision(glm::vec3& newPosition, const std::vector<AABB>& colliders)
{
    AABB playerBounds = AABB::fromCenterSize(
        newPosition,
        glm::vec3(m_config.playerRadius * 2.0f, m_config.playerHeight, m_config.playerRadius * 2.0f)
    );

    for (const auto& collider : colliders)
    {
        if (playerBounds.intersects(collider))
        {
            glm::vec3 pushOut = playerBounds.getMinPushOut(collider);
            newPosition += pushOut;

            // Recompute player bounds after push
            playerBounds = AABB::fromCenterSize(
                newPosition,
                glm::vec3(m_config.playerRadius * 2.0f, m_config.playerHeight, m_config.playerRadius * 2.0f)
            );
        }
    }
}

float FirstPersonController::applyDeadzone(float value) const
{
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
    return AABB::fromCenterSize(
        m_camera.getPosition(),
        glm::vec3(m_config.playerRadius * 2.0f, m_config.playerHeight, m_config.playerRadius * 2.0f)
    );
}

} // namespace Vestige
