// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file input_manager.h
/// @brief Keyboard, mouse, and gamepad input handling.
#pragma once

#include "core/event_bus.h"
#include "input/input_bindings.h"

#include <glm/glm.hpp>

#include <string>

struct GLFWwindow;

namespace Vestige
{

/// @brief Manages all input state — keyboard, mouse, and gamepads.
class InputManager
{
public:
    /// @brief Creates the input manager and registers GLFW callbacks.
    /// @param window The GLFW window to capture input from.
    /// @param eventBus Event bus for publishing input events.
    InputManager(GLFWwindow* window, EventBus& eventBus);

    /// @brief Updates input state. Call once per frame.
    void update();

    /// @brief Checks if a key is currently held down.
    /// @param keyCode GLFW key code (e.g., GLFW_KEY_W).
    /// @return True if the key is pressed.
    bool isKeyDown(int keyCode) const;

    /// @brief Gets the current mouse position.
    /// @return Mouse position in screen coordinates.
    glm::vec2 getMousePosition() const;

    /// @brief Gets the mouse movement since the last frame.
    /// @return Mouse delta (x, y).
    glm::vec2 getMouseDelta() const;

    /// @brief Checks if a mouse button is currently held down.
    /// @param button GLFW mouse button (e.g., GLFW_MOUSE_BUTTON_LEFT).
    /// @return True if the button is pressed.
    bool isMouseButtonDown(int button) const;

    /// @brief Gets the scroll wheel offset since the last frame.
    /// @return Scroll delta (x, y).
    glm::vec2 getScrollDelta() const;

    // -- Action-map integration (Phase 10 remappable controls) --

    /// @brief Polls a single `InputBinding` against the current GLFW
    ///        state. Keyboard + mouse bindings are answered directly;
    ///        gamepad bindings check every connected joystick slot
    ///        (GLFW_JOYSTICK_1..LAST) so the user doesn't have to pick
    ///        which controller they plugged in.
    /// @returns true if the physical input is currently held.
    bool isBindingDown(const InputBinding& binding) const;

    /// @brief Convenience wrapper — true iff any slot of @a actionId
    ///        in @a map is currently down.
    bool isActionDown(const InputActionMap& map,
                      const std::string& actionId) const;

private:
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPositionCallback(GLFWwindow* window, double xPos, double yPos);
    static void scrollCallback(GLFWwindow* window, double xOffset, double yOffset);

    GLFWwindow* m_window;
    EventBus& m_eventBus;

    glm::vec2 m_mousePosition;
    glm::vec2 m_lastMousePosition;
    glm::vec2 m_mouseDelta;
    glm::vec2 m_scrollDelta;
    bool m_isFirstMouse;
};

} // namespace Vestige
