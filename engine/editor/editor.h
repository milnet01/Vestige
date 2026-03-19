/// @file editor.h
/// @brief Editor subsystem — ImGui integration, docking, and editor mode management.
#pragma once

#include <string>

struct GLFWwindow;

namespace Vestige
{

class EventBus;

/// @brief Editor/Play mode toggle.
enum class EditorMode
{
    EDIT,   ///< Editor UI visible, orbit camera, gizmos active.
    PLAY    ///< First-person walkthrough, cursor captured, no editor UI.
};

/// @brief Manages the ImGui editor overlay and editor state.
class Editor
{
public:
    Editor();
    ~Editor();

    // Non-copyable
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    /// @brief Initializes ImGui with GLFW + OpenGL backend, dark theme, docking.
    /// @param window GLFW window handle.
    /// @param assetPath Base path to assets directory (for loading editor font).
    /// @return True if initialization succeeded.
    bool initialize(GLFWwindow* window, const std::string& assetPath);

    /// @brief Shuts down ImGui and releases resources.
    void shutdown();

    /// @brief Starts a new ImGui frame. Call after Renderer::endFrame().
    /// In EDIT mode, draws the dockspace and editor panels.
    void beginFrame();

    /// @brief Finalizes and renders the ImGui frame. Call before Window::swapBuffers().
    void endFrame();

    /// @brief Sets the editor mode (EDIT or PLAY).
    void setMode(EditorMode mode);

    /// @brief Gets the current editor mode.
    EditorMode getMode() const;

    /// @brief Toggles between EDIT and PLAY mode.
    void toggleMode();

    /// @brief Returns true if ImGui wants to capture mouse input.
    bool wantCaptureMouse() const;

    /// @brief Returns true if ImGui wants to capture keyboard input.
    bool wantCaptureKeyboard() const;

    /// @brief Returns true if the editor has been initialized.
    bool isInitialized() const;

private:
    void setupTheme();

    EditorMode m_mode = EditorMode::EDIT;
    bool m_isInitialized = false;
    bool m_showDemoWindow = true;
    GLFWwindow* m_window = nullptr;
};

} // namespace Vestige
