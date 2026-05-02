// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file input_manager.cpp
/// @brief Input manager implementation.
#include "core/input_manager.h"
#include "core/logger.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace Vestige
{

InputManager::InputManager(GLFWwindow* window, EventBus& eventBus)
    : m_window(window)
    , m_eventBus(eventBus)
    , m_mousePosition(0.0f)
    , m_lastMousePosition(0.0f)
    , m_mouseDelta(0.0f)
    , m_scrollDelta(0.0f)
    , m_isFirstMouse(true)
{
    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, cursorPositionCallback);
    glfwSetScrollCallback(m_window, scrollCallback);

    Logger::debug("Input manager initialized");
}

void InputManager::update()
{
    // Reset per-frame deltas
    m_mouseDelta = glm::vec2(0.0f);
    m_scrollDelta = glm::vec2(0.0f);
}

bool InputManager::isKeyDown(int keyCode) const
{
    return glfwGetKey(m_window, keyCode) == GLFW_PRESS;
}

glm::vec2 InputManager::getMousePosition() const
{
    return m_mousePosition;
}

glm::vec2 InputManager::getMouseDelta() const
{
    return m_mouseDelta;
}

bool InputManager::isMouseButtonDown(int button) const
{
    return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
}

glm::vec2 InputManager::getScrollDelta() const
{
    return m_scrollDelta;
}

bool InputManager::isBindingDown(const InputBinding& binding) const
{
    if (!binding.isBound() || m_window == nullptr) return false;

    switch (binding.device)
    {
        case InputDevice::Keyboard:
        {
            // Phase 10.9 Slice 9 I1 — keyboard bindings store scancodes
            // (physical key position). `glfwGetKey` polls by keycode
            // (layout-mapped), so we drive a scancode-indexed table from
            // the keyCallback's `scancode` param instead.
            auto it = m_keyDownByScancode.find(binding.code);
            return it != m_keyDownByScancode.end() && it->second;
        }

        case InputDevice::Mouse:
            return glfwGetMouseButton(m_window, binding.code) == GLFW_PRESS;

        case InputDevice::Gamepad:
        {
            // Check every connected gamepad slot — the user shouldn't
            // have to pick which controller is "player 1" before
            // remapped bindings work. Single-player convenience; a
            // future multiplayer split binds a specific joystick id.
            for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
            {
                if (glfwJoystickPresent(jid) == GLFW_FALSE) continue;
                if (glfwJoystickIsGamepad(jid) == GLFW_FALSE) continue;
                GLFWgamepadstate state;
                if (glfwGetGamepadState(jid, &state) == GLFW_FALSE) continue;
                if (binding.code < 0
                    || binding.code >= static_cast<int>(sizeof(state.buttons)))
                {
                    continue;
                }
                if (state.buttons[binding.code] == GLFW_PRESS) return true;
            }
            return false;
        }

        case InputDevice::GamepadAxis:
            // Slice 9 I3 — axis half is "down" when its signed deflection
            // crosses the digital threshold. `bindingAxisValue` already
            // does the joystick scan + sign fold + trigger normalisation.
            return bindingAxisValue(binding) >= AXIS_DIGITAL_THRESHOLD;

        case InputDevice::None:
            return false;
    }
    return false;
}

float InputManager::bindingAxisValue(const InputBinding& binding) const
{
    if (!binding.isBound() || m_window == nullptr) return 0.0f;

    if (binding.device == InputDevice::GamepadAxis)
    {
        const int axis = unpackGamepadAxisIndex(binding.code);
        const int sign = unpackGamepadAxisSign(binding.code);
        const bool isTrigger = (axis == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER
                             || axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER);

        float best = 0.0f;
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
        {
            if (glfwJoystickPresent(jid) == GLFW_FALSE) continue;
            if (glfwJoystickIsGamepad(jid) == GLFW_FALSE) continue;
            GLFWgamepadstate state;
            if (glfwGetGamepadState(jid, &state) == GLFW_FALSE) continue;
            if (axis < 0
                || axis >= static_cast<int>(sizeof(state.axes) / sizeof(state.axes[0])))
            {
                continue;
            }
            float raw = state.axes[axis];
            if (isTrigger)
            {
                // GLFW reports triggers as [-1, 1] with -1 = released.
                // Map to [0, 1].
                raw = (raw + 1.0f) * 0.5f;
            }
            const float signed_v = static_cast<float>(sign) * raw;
            if (signed_v > best) best = signed_v;
        }
        return std::clamp(best, 0.0f, 1.0f);
    }

    // Digital slots — promote the on/off poll to 0.0 / 1.0.
    return isBindingDown(binding) ? 1.0f : 0.0f;
}

bool InputManager::isActionDown(const InputActionMap& map,
                                const std::string& actionId) const
{
    return Vestige::isActionDown(map, actionId,
        [this](const InputBinding& b) { return isBindingDown(b); });
}

float InputManager::actionAxisValue(const InputActionMap& map,
                                    const std::string& actionId) const
{
    return Vestige::axisValue(map, actionId,
        [this](const InputBinding& b) { return bindingAxisValue(b); });
}

void InputManager::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!self)
    {
        return;
    }

    // I1 — keep the scancode-indexed state table current regardless of
    // whether GLFW could resolve the keycode (some platform-specific
    // keys arrive as GLFW_KEY_UNKNOWN but still carry a valid scancode).
    if (scancode >= 0)
    {
        if (action == GLFW_PRESS)
        {
            self->m_keyDownByScancode[scancode] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            self->m_keyDownByScancode[scancode] = false;
        }
        // GLFW_REPEAT leaves the down-flag as-is (already true).
    }

    if (key == GLFW_KEY_UNKNOWN)
    {
        return;
    }

    // S4: `mods` forwarded so subscribers can tell `Tab` from `Shift+Tab`
    // without re-querying GLFW (which would race against auto-repeat).
    if (action == GLFW_PRESS)
    {
        self->m_eventBus.publish(KeyPressedEvent(key, false, mods));
    }
    else if (action == GLFW_REPEAT)
    {
        self->m_eventBus.publish(KeyPressedEvent(key, true, mods));
    }
    else if (action == GLFW_RELEASE)
    {
        self->m_eventBus.publish(KeyReleasedEvent(key));
    }
}

void InputManager::mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/)
{
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!self)
    {
        return;
    }

    if (action == GLFW_PRESS)
    {
        self->m_eventBus.publish(MouseButtonPressedEvent(button));
    }
    else if (action == GLFW_RELEASE)
    {
        self->m_eventBus.publish(MouseButtonReleasedEvent(button));
    }
}

void InputManager::cursorPositionCallback(GLFWwindow* window, double xPos, double yPos)
{
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!self)
    {
        return;
    }

    auto newPos = glm::vec2(static_cast<float>(xPos), static_cast<float>(yPos));

    if (self->m_isFirstMouse)
    {
        self->m_lastMousePosition = newPos;
        self->m_isFirstMouse = false;
    }

    self->m_mouseDelta += newPos - self->m_lastMousePosition;
    self->m_lastMousePosition = newPos;
    self->m_mousePosition = newPos;

    self->m_eventBus.publish(MouseMovedEvent(xPos, yPos));
}

void InputManager::scrollCallback(GLFWwindow* window, double xOffset, double yOffset)
{
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!self)
    {
        return;
    }

    self->m_scrollDelta += glm::vec2(static_cast<float>(xOffset), static_cast<float>(yOffset));
    self->m_eventBus.publish(MouseScrollEvent(xOffset, yOffset));
}

} // namespace Vestige
