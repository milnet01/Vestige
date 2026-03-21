/// @file editor.h
/// @brief Editor subsystem — ImGui integration, docking, editor camera, and mode management.
#pragma once

#include "editor/command_history.h"
#include "editor/editor_camera.h"
#include "editor/entity_actions.h"
#include "editor/entity_factory.h"
#include "editor/file_menu.h"
#include "editor/panels/asset_browser_panel.h"
#include "editor/panels/hierarchy_panel.h"
#include "editor/panels/history_panel.h"
#include "editor/panels/import_dialog.h"
#include "editor/panels/inspector_panel.h"
#include "editor/prefab_system.h"
#include "editor/selection.h"

#include <imgui.h>
#include <ImGuizmo.h>

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
class ResourceManager;
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
    /// @param camera Camera for gizmo projection. May be nullptr.
    void drawPanels(Renderer* renderer, Scene* scene, Camera* camera = nullptr);

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

    /// @brief Gets the viewport panel size in pixels (from previous frame).
    /// @param outWidth Set to viewport width.
    /// @param outHeight Set to viewport height.
    void getViewportSize(int& outWidth, int& outHeight) const;

    /// @brief Returns true if the gizmo was hovered or used last frame.
    /// Used to suppress viewport picks when interacting with the gizmo.
    bool isGizmoActive() const;

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

    /// @brief Stores a pointer to the ResourceManager for entity spawning.
    /// @param resourceManager ResourceManager owned by Engine. Must outlive Editor.
    void setResourceManager(ResourceManager* resourceManager);

    /// @brief Gets the selection state.
    Selection& getSelection();
    const Selection& getSelection() const;

    /// @brief Gets the file menu (for save/load, dirty tracking, quit).
    FileMenu& getFileMenu();
    const FileMenu& getFileMenu() const;

    /// @brief Gets the command history (for undo/redo).
    CommandHistory& getCommandHistory();
    const CommandHistory& getCommandHistory() const;

private:
    void setupTheme();
    void drawGizmo(Camera* camera, Scene* scene);
    void drawGizmoOverlay();
    void processGizmoShortcuts();
    void processEntityShortcuts(Scene* scene);

    EditorMode m_mode = EditorMode::EDIT;
    bool m_isInitialized = false;
    bool m_showDemoWindow = false;
    bool m_viewportFocused = false;
    bool m_viewportHovered = false;
    GLFWwindow* m_window = nullptr;

    std::unique_ptr<EditorCamera> m_editorCamera;
    Selection m_selection;
    FileMenu m_fileMenu;
    CommandHistory m_commandHistory;
    HierarchyPanel m_hierarchyPanel;
    HistoryPanel m_historyPanel;
    InspectorPanel m_inspectorPanel;
    ImportDialog m_importDialog;
    AssetBrowserPanel m_assetBrowserPanel;
    PrefabSystem m_prefabSystem;
    ResourceManager* m_resourceManager = nullptr;
    std::string m_assetPath;

    // Viewport bounds (stored from drawPanels, used next frame for click detection)
    glm::vec2 m_viewportMin = glm::vec2(0.0f);
    glm::vec2 m_viewportMax = glm::vec2(0.0f);

    // Pick request state
    bool m_pickRequested = false;
    int m_pickX = 0;
    int m_pickY = 0;
    bool m_pickShift = false;
    bool m_pickCtrl = false;

    // Gizmo state
    ImGuizmo::OPERATION m_gizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_gizmoMode = ImGuizmo::WORLD;
    bool m_gizmoActive = false;  ///< Was gizmo hovered/used last frame (for pick suppression).
    bool m_wasGizmoUsing = false; ///< Was gizmo being manipulated last frame (for dirty tracking).
    glm::vec3 m_gizmoStartPosition = glm::vec3(0.0f);  ///< Transform at gizmo drag start.
    glm::vec3 m_gizmoStartRotation = glm::vec3(0.0f);
    glm::vec3 m_gizmoStartScale = glm::vec3(1.0f);
    uint32_t m_gizmoStartEntityId = 0; ///< Entity being transformed at drag start.
    float m_snapTranslation = 0.5f;  ///< Snap grid in world units.
    float m_snapRotation = 15.0f;    ///< Snap in degrees.
    float m_snapScale = 0.1f;        ///< Snap in scale units.

    // Transform clipboard (Ctrl+Shift+C / Ctrl+Shift+V)
    EntityActions::TransformClipboard m_transformClipboard;
};

} // namespace Vestige
