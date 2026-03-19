/// @file editor.h
/// @brief Editor subsystem — ImGui integration, docking, editor camera, and mode management.
#pragma once

#include "editor/editor_camera.h"
#include "editor/panels/hierarchy_panel.h"
#include "editor/panels/inspector_panel.h"
#include "editor/selection.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>

struct GLFWwindow;

namespace Vestige
{

class Camera;
class EventBus;
class Renderer;
class Scene;

/// @brief Editor/Play mode toggle.
enum class EditorMode
{
    EDIT,   ///< Editor UI visible, orbit camera, gizmos active.
    PLAY    ///< First-person walkthrough, cursor captured, no editor UI.
};

/// @brief Manages the ImGui editor overlay, editor camera, and editor state.
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

    /// @brief Starts a new ImGui frame. Call early in the frame for input state.
    /// Must be called before processInput/drawPanels.
    void prepareFrame();

    /// @brief Draws the editor UI panels (dockspace, viewport, hierarchy, etc.).
    /// Must call prepareFrame() first.
    /// @param renderer Renderer reference (for viewport texture). May be nullptr.
    /// @param scene Active scene for hierarchy panel. May be nullptr.
    void drawPanels(Renderer* renderer, Scene* scene);

    /// @brief Finalizes and renders the ImGui frame. Call before Window::swapBuffers().
    void endFrame();

    /// @brief Updates the editor camera from input and applies smooth transitions.
    /// @param deltaTime Time since last frame.
    void updateEditorCamera(float deltaTime);

    /// @brief Applies the editor camera state to the given game camera.
    /// @param camera The camera used for rendering.
    void applyEditorCamera(Camera& camera);

    /// @brief Gets the editor camera (for view presets, focus, etc.).
    EditorCamera* getEditorCamera();

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

    /// @brief Checks for viewport clicks and computes FBO pick coordinates.
    /// Call after prepareFrame() and before rendering.
    /// @param fboWidth Width of the render FBO in pixels.
    /// @param fboHeight Height of the render FBO in pixels.
    void processViewportClick(int fboWidth, int fboHeight);

    /// @brief Returns true if a pick request is pending this frame.
    bool isPickRequested() const;

    /// @brief Gets the FBO-space pick coordinates (valid when isPickRequested() is true).
    void getPickCoords(int& outX, int& outY) const;

    /// @brief Handles the result of an ID buffer pick.
    /// Applies Shift/Ctrl modifier logic to update the selection.
    /// @param entityId The entity ID under the cursor (0 = background).
    void handlePickResult(uint32_t entityId);

    /// @brief Gets the selection state.
    Selection& getSelection();
    const Selection& getSelection() const;

private:
    void setupTheme();

    EditorMode m_mode = EditorMode::EDIT;
    bool m_isInitialized = false;
    bool m_showDemoWindow = false;
    bool m_viewportFocused = false;
    bool m_viewportHovered = false;
    GLFWwindow* m_window = nullptr;

    std::unique_ptr<EditorCamera> m_editorCamera;
    Selection m_selection;
    HierarchyPanel m_hierarchyPanel;
    InspectorPanel m_inspectorPanel;

    // Viewport bounds (stored from drawPanels, used next frame for click detection)
    glm::vec2 m_viewportMin = glm::vec2(0.0f);
    glm::vec2 m_viewportMax = glm::vec2(0.0f);

    // Pick request state
    bool m_pickRequested = false;
    int m_pickX = 0;
    int m_pickY = 0;
    bool m_pickShift = false;
    bool m_pickCtrl = false;
};

} // namespace Vestige
