// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file window.h
/// @brief GLFW window management and OpenGL context.
#pragma once

#include "core/event_bus.h"

#include <string>

struct GLFWwindow;

namespace Vestige
{

/// @brief Configuration for creating a window.
struct WindowConfig
{
    std::string title = "Vestige";
    int width = 1280;
    int height = 720;
    bool isVsyncEnabled = true;
};

/// @brief Manages the GLFW window and OpenGL context.
class Window
{
public:
    /// @brief Creates a window with the given configuration.
    /// @param config Window settings (title, size, vsync).
    /// @param eventBus Event bus for publishing window events.
    explicit Window(const WindowConfig& config, EventBus& eventBus);
    ~Window();

    // Non-copyable, non-movable (RAII resource)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    /// @brief Polls OS events (keyboard, mouse, window events).
    static void pollEvents();

    /// @brief Swaps the front and back framebuffers.
    void swapBuffers();

    /// @brief Checks if the window should close.
    /// @return True if the close button was pressed or close was requested.
    bool shouldClose() const;

    /// @brief Gets the current window width in pixels.
    int getWidth() const;

    /// @brief Gets the current window height in pixels.
    int getHeight() const;

    /// @brief Gets the raw GLFW window pointer (for subsystems that need it).
    GLFWwindow* getHandle() const;

    /// @brief Enables or disables the mouse cursor (for FPS-style controls).
    /// @param isEnabled True to show the cursor, false to capture it.
    void setCursorEnabled(bool isEnabled);

    /// @brief Saves the current window position and size to a config file.
    /// Call on shutdown so the window restores its state next launch.
    void saveWindowState() const;

    /// @brief Enables or disables vertical sync.
    void setVsync(bool enabled);

    /// @brief Returns whether vsync is currently enabled.
    bool isVsyncEnabled() const;

    /// @brief Applies a new video mode (resolution / fullscreen / vsync) at
    ///        runtime. The GL context survives the transition — GLFW's
    ///        `glfwSetWindowMonitor` preserves it across a windowed ↔
    ///        fullscreen toggle.
    ///
    /// The framebuffer-resize callback fires once the new mode is live, so
    /// any subsystem subscribed to `WindowResizeEvent` re-sizes its
    /// offscreen buffers automatically.
    ///
    /// @param width      New framebuffer width in pixels (must be > 0).
    /// @param height     New framebuffer height in pixels (must be > 0).
    /// @param fullscreen True = exclusive fullscreen on the primary monitor;
    ///                   false = windowed.
    /// @param vsync      True = wait for vblank on swap; false = unlimited.
    void setVideoMode(int width, int height, bool fullscreen, bool vsync);

    /// @brief Returns whether the window is currently fullscreen.
    bool isFullscreen() const;

private:
    /// @brief Restores window position and size from the config file.
    /// Called during construction after the window is created.
    void restoreWindowState();
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);

    /// @brief Static instance pointer for GLFW callbacks.
    /// Safe because only one Window exists (Engine owns a single Window).
    /// This avoids the glfwSetWindowUserPointer conflict with InputManager.
    static Window* s_instance;

    GLFWwindow* m_handle;
    int m_width;
    int m_height;
    bool m_vsyncEnabled = true;
    // Windowed-mode geometry remembered while fullscreen, so a toggle
    // back to windowed restores the previous window rectangle instead
    // of falling back to GLFW's default (top-left 0,0).
    int m_savedWindowedX = 100;
    int m_savedWindowedY = 100;
    int m_savedWindowedWidth  = 1280;
    int m_savedWindowedHeight = 720;
    EventBus& m_eventBus;
};

} // namespace Vestige
