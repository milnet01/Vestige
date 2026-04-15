// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file input_manager.cpp
/// @brief Input manager implementation.
#include "core/input_manager.h"
#include "core/logger.h"

#include <GLFW/glfw3.h>

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

void InputManager::keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!self || key == GLFW_KEY_UNKNOWN)
    {
        return;
    }

    if (action == GLFW_PRESS)
    {
        self->m_eventBus.publish(KeyPressedEvent(key, false));
    }
    else if (action == GLFW_REPEAT)
    {
        self->m_eventBus.publish(KeyPressedEvent(key, true));
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
