// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file editor.h
/// @brief Editor subsystem — ImGui integration, docking, editor camera, and mode management.
#pragma once

#include "editor/command_history.h"
#include "editor/editor_camera.h"
#include "editor/entity_actions.h"
#include "editor/entity_factory.h"
#include "editor/file_menu.h"
#include "editor/panels/asset_browser_panel.h"
#include "editor/panels/environment_panel.h"
#include "editor/panels/terrain_panel.h"
#include "editor/panels/hierarchy_panel.h"
#include "editor/panels/performance_panel.h"
#include "editor/panels/history_panel.h"
#include "editor/panels/import_dialog.h"
#include "editor/panels/inspector_panel.h"
#include "editor/panels/script_editor_panel.h"
#include "scripting/node_type_registry.h"
#include "editor/panels/validation_panel.h"
#include "editor/panels/first_run_wizard.h"
#include "editor/panels/settings_editor_panel.h"
#include "editor/panels/welcome_panel.h"
#include "editor/panels/texture_viewer_panel.h"
#include "editor/panels/hdri_viewer_panel.h"
#include "editor/panels/model_viewer_panel.h"
#include "editor/panels/navigation_panel.h"
#include "editor/panels/audio_panel.h"
#include "editor/panels/sprite_panel.h"
#include "editor/panels/tilemap_panel.h"
#include "editor/panels/ui_layout_panel.h"
#include "editor/panels/ui_runtime_panel.h"
#include "editor/panels/template_dialog.h"
#include "editor/prefab_system.h"
#include "editor/selection.h"
#include "editor/tools/brush_tool.h"
#include "editor/tools/brush_preview.h"
#include "editor/tools/cutout_tool.h"
#include "editor/tools/path_tool.h"
#include "editor/tools/roof_tool.h"
#include "editor/tools/room_tool.h"
#include "editor/tools/ruler_tool.h"
#include "editor/tools/stair_tool.h"
#include "editor/tools/terrain_brush.h"
#include "editor/tools/wall_tool.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

struct GLFWwindow;

namespace Vestige
{

class Camera;
class EventBus;
class AudioSystem;
class UISystem;
class FoliageManager;
class NavigationSystem;
class PerformanceProfiler;
class Renderer;
class ResourceManager;
class Scene;
class Terrain;

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
    void drawPanels(Renderer* renderer, Scene* scene, Camera* camera = nullptr,
                    Timer* timer = nullptr, Window* window = nullptr);

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

    /// @brief Gets the viewport minimum screen position (top-left corner).
    glm::vec2 getViewportMin() const { return m_viewportMin; }

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

    /// @brief Gets the brush tool (for environment painting).
    BrushTool& getBrushTool();

    /// @brief Gets the brush preview renderer.
    BrushPreviewRenderer& getBrushPreview();

    /// @brief Gets the environment panel.
    EnvironmentPanel& getEnvironmentPanel();

    /// @brief Stores a pointer to the FoliageManager for brush painting.
    void setFoliageManager(FoliageManager* manager);

    /// @brief Stores a pointer to the Terrain for sculpting/painting.
    void setTerrain(Terrain* terrain);

    /// @brief Stores a pointer to the PerformanceProfiler.
    void setProfiler(PerformanceProfiler* profiler);

    /// @brief Stores a pointer to the NavigationSystem (for the Navigation panel).
    void setNavigationSystem(NavigationSystem* navSystem);

    /// @brief Stores a pointer to the AudioSystem (for the Audio panel).
    void setAudioSystem(AudioSystem* audioSystem);

    /// @brief Stores a pointer to the UISystem (for the UI Runtime panel).
    void setUISystem(UISystem* uiSystem);

    /// @brief Gets the terrain brush tool.
    TerrainBrush& getTerrainBrush();

    /// @brief Gets the terrain panel.
    TerrainPanel& getTerrainPanel();

    /// @brief Gets the performance panel.
    PerformancePanel& getPerformancePanel();

    /// @brief Gets the validation panel.
    ValidationPanel& getValidationPanel() { return m_validationPanel; }

    /// @brief Gets the welcome panel.
    WelcomePanel& getWelcomePanel() { return m_welcomePanel; }

    /// @brief Gets the first-run wizard (Phase 10.5 slice 14.2 / 14.4).
    FirstRunWizard& getFirstRunWizard() { return m_firstRunWizard; }

    /// @brief Wires the onboarding settings + asset root into the
    ///        first-run wizard, and supplies a callback for the
    ///        "Show me the Demo" button (dispatches to
    ///        `Engine::setupDemoScene` in production).
    ///
    /// Slice 14.4 engine-wiring entry point. Editor does NOT own
    /// `Settings`; `engine/core/engine.cpp` loads the struct and
    /// passes the `onboarding` sub-struct pointer in here so the
    /// wizard reads + writes in place. The caller is responsible
    /// for saving `Settings` after the wizard closes with
    /// `hasCompletedFirstRun == true` — Editor emits a
    /// `m_wizardJustClosed` flag each frame that the engine layer
    /// polls via `consumeWizardJustClosed()`.
    void wireFirstRunWizard(OnboardingSettings* onboarding,
                            std::filesystem::path assetRoot,
                            std::function<void()> applyDemoCallback);

    /// @brief Edge-triggered: true on the first frame after the
    ///        wizard closed. Engine polls this to trigger a
    ///        `Settings::saveAtomic`.
    bool consumeWizardJustClosed();

    /// @brief Gets the Settings editor panel (Phase 10 slice 13.5b).
    SettingsEditorPanel& getSettingsEditorPanel() { return m_settingsEditorPanel; }

    /// @brief Wire the settings panel to the engine's
    ///        `SettingsEditor` + input map + on-disk path. Called
    ///        from `Engine::initialize` once all those exist.
    void wireSettingsEditorPanel(SettingsEditor* editor,
                                  InputActionMap* inputMap,
                                  std::filesystem::path settingsPath);

    /// @brief Gets the visual script editor panel (Phase 9E-3).
    ScriptEditorPanel& getScriptEditorPanel() { return m_scriptEditorPanel; }

    /// @brief Gets the ruler tool.
    RulerTool& getRulerTool() { return m_rulerTool; }

    /// @brief Gets the architectural tools.
    WallTool& getWallTool() { return m_wallTool; }
    RoomTool& getRoomTool() { return m_roomTool; }
    CutoutTool& getCutoutTool() { return m_cutoutTool; }
    RoofTool& getRoofTool() { return m_roofTool; }
    StairTool& getStairTool() { return m_stairTool; }
    PathTool& getPathTool() { return m_pathTool; }

    /// @brief Gets the asset viewer panels.
    TextureViewerPanel& getTextureViewerPanel() { return m_textureViewerPanel; }
    HdriViewerPanel& getHdriViewerPanel() { return m_hdriViewerPanel; }
    ModelViewerPanel& getModelViewerPanel() { return m_modelViewerPanel; }

    /// @brief Gets the navigation (navmesh) panel.
    NavigationPanel& getNavigationPanel() { return m_navigationPanel; }
    const NavigationPanel& getNavigationPanel() const { return m_navigationPanel; }

    /// @brief Gets the audio (mixer / zones / debug) panel.
    AudioPanel& getAudioPanel() { return m_audioPanel; }
    const AudioPanel& getAudioPanel() const { return m_audioPanel; }

    /// @brief Gets the sprite atlas panel (Phase 9F-6 — wired by W14).
    SpritePanel& getSpritePanel() { return m_spritePanel; }
    const SpritePanel& getSpritePanel() const { return m_spritePanel; }

    /// @brief Gets the tilemap layer / palette panel (Phase 9F-6 — wired by W14).
    TilemapPanel& getTilemapPanel() { return m_tilemapPanel; }
    const TilemapPanel& getTilemapPanel() const { return m_tilemapPanel; }

    /// @brief Gets the UI layout / theme editor panel.
    UILayoutPanel& getUILayoutPanel() { return m_uiLayoutPanel; }
    const UILayoutPanel& getUILayoutPanel() const { return m_uiLayoutPanel; }

    /// @brief Gets the in-game UI runtime panel (screen state / menu
    ///        preview / HUD toggles / accessibility composition).
    UIRuntimePanel& getUIRuntimePanel() { return m_uiRuntimePanel; }
    const UIRuntimePanel& getUIRuntimePanel() const { return m_uiRuntimePanel; }

private:
    static void setupTheme();
    void drawGizmo(Camera* camera, Scene* scene);
    void drawGizmoOverlay();
    void processGizmoShortcuts();
    void processEntityShortcuts(Scene* scene);

    EditorMode m_mode = EditorMode::EDIT;
    bool m_isInitialized = false;
    bool m_showDemoWindow = false;
    bool m_showControlsWindow = false;
    bool m_showGrid = true;       ///< Ground-plane grid overlay (1m thin, 10m bold).
    bool m_showConsole = true;    ///< Console/log panel visibility.
    bool m_showStatistics = false; ///< Scene statistics panel visibility.
    bool m_showAllLightGizmos = false; ///< Draw gizmos for all lights, not just selected.
    bool m_captureScreenshotRequested = false; ///< Menu-triggered screenshot request.
    bool m_fullscreenViewport = false; ///< Hide all panels for clean viewport.

    // Play mode render resolution
    int m_playModeWidth = 1920;
    int m_playModeHeight = 1080;

    // Box selection drag state
    bool m_boxSelectActive = false;
    glm::vec2 m_boxSelectStart = glm::vec2(0.0f);
    glm::vec2 m_boxSelectEnd = glm::vec2(0.0f);
    bool m_boxSelectPending = false;
    int m_boxPickX0 = 0, m_boxPickY0 = 0, m_boxPickX1 = 0, m_boxPickY1 = 0;
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
    EnvironmentPanel m_environmentPanel;
    TerrainPanel m_terrainPanel;
    PerformancePanel m_performancePanel;
    ValidationPanel m_validationPanel;
    WelcomePanel m_welcomePanel;
    FirstRunWizard m_firstRunWizard;
    SettingsEditorPanel m_settingsEditorPanel;
    std::function<void()> m_applyDemoCallback;
    bool m_wizardWasOpenLastFrame = false;
    bool m_wizardJustClosedThisFrame = false;
    ScriptEditorPanel m_scriptEditorPanel;
    // Editor-owned node registry that feeds the ScriptEditorPanel. The engine
    // runtime doesn't currently spin up a ScriptingSystem (scripts are
    // exercised only via unit tests), so the editor owns its own registry
    // populated from the same register* entry points. Lives next to the
    // panel so the pointer handed via setRegistry() outlives every draw.
    NodeTypeRegistry m_nodeTypeRegistry;
    TextureViewerPanel m_textureViewerPanel;
    HdriViewerPanel m_hdriViewerPanel;
    ModelViewerPanel m_modelViewerPanel;
    NavigationPanel m_navigationPanel;
    AudioPanel m_audioPanel;
    SpritePanel m_spritePanel;
    TilemapPanel m_tilemapPanel;
    UILayoutPanel m_uiLayoutPanel;
    UIRuntimePanel m_uiRuntimePanel;
    TemplateDialog m_templateDialog;
    BrushTool m_brushTool;
    BrushPreviewRenderer m_brushPreview;
    RulerTool m_rulerTool;
    TerrainBrush m_terrainBrush;
    WallTool m_wallTool;
    RoomTool m_roomTool;
    CutoutTool m_cutoutTool;
    RoofTool m_roofTool;
    StairTool m_stairTool;
    PathTool m_pathTool;
    PrefabSystem m_prefabSystem;
    ResourceManager* m_resourceManager = nullptr;
    FoliageManager* m_foliageManager = nullptr;
    Terrain* m_terrain = nullptr;
    PerformanceProfiler* m_profiler = nullptr;
    NavigationSystem* m_navigationSystem = nullptr;
    AudioSystem* m_audioSystem = nullptr;
    UISystem* m_uiSystem = nullptr;
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

    // On-screen notification overlay (brief message, auto-fades)
    std::string m_notifyText;
    float m_notifyTimer = 0.0f;

public:
    /// @brief Shows a brief notification overlay that fades after ~2 seconds.
    void showNotification(const std::string& text);

    /// @brief Returns true if all light gizmos should be drawn (not just selected).
    bool isShowAllLightGizmos() const { return m_showAllLightGizmos; }

    /// @brief Toggles drawing gizmos for all lights vs. selected only.
    void toggleShowAllLightGizmos() { m_showAllLightGizmos = !m_showAllLightGizmos; }

    /// @brief Returns true if the ground grid overlay should be rendered.
    bool isGridVisible() const { return m_showGrid; }

    /// @brief Toggles the ground grid overlay.
    void toggleGrid() { m_showGrid = !m_showGrid; }

    /// @brief Gets/sets the play mode render resolution (independent of window size).
    int getPlayModeWidth() const { return m_playModeWidth; }
    int getPlayModeHeight() const { return m_playModeHeight; }
    void setPlayModeResolution(int w, int h) { m_playModeWidth = w; m_playModeHeight = h; }

    /// @brief Returns true if a box-select region pick is pending.
    bool isBoxSelectPending() const { return m_boxSelectPending; }

    /// @brief Gets and consumes the box-select FBO-space rectangle.
    /// Returns false if no pending request.
    bool consumeBoxSelect(int& x0, int& y0, int& x1, int& y1)
    {
        if (!m_boxSelectPending) return false;
        m_boxSelectPending = false;
        x0 = m_boxPickX0; y0 = m_boxPickY0;
        x1 = m_boxPickX1; y1 = m_boxPickY1;
        return true;
    }

    /// @brief Returns true (and resets) if a screenshot was requested via menu.
    bool consumeScreenshotRequest()
    {
        bool r = m_captureScreenshotRequested;
        m_captureScreenshotRequested = false;
        return r;
    }
};

} // namespace Vestige
