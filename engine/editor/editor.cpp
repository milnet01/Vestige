// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file editor.cpp
/// @brief Editor implementation — ImGui lifecycle, docking workspace, editor camera, theme.
#include "editor/editor.h"
#include "editor/commands/transform_command.h"
#include "editor/commands/create_entity_command.h"
#include "editor/commands/delete_entity_command.h"
#include "editor/commands/composite_command.h"
#include "editor/commands/entity_property_command.h"
#include "core/logger.h"
#include "core/timer.h"
#include "renderer/camera.h"
#include "renderer/renderer.h"
#include "resource/resource_manager.h"
#include "scene/entity.h"
#include "scene/light_component.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"
#include "systems/navigation_system.h"

// Visual-scripting node palette registrations — free functions defined in
// the scripting/*_nodes.cpp translation units. Forward-declared here rather
// than via a dedicated header so the editor doesn't pull every scripting
// header transitively; the registry type itself is needed and included by
// editor.h.
namespace Vestige {
void registerCoreNodeTypes(NodeTypeRegistry& registry);
void registerEventNodeTypes(NodeTypeRegistry& registry);
void registerActionNodeTypes(NodeTypeRegistry& registry);
void registerPureNodeTypes(NodeTypeRegistry& registry);
void registerFlowNodeTypes(NodeTypeRegistry& registry);
void registerLatentNodeTypes(NodeTypeRegistry& registry);
}

#include <functional>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace Vestige
{

Editor::Editor() = default;

Editor::~Editor()
{
    shutdown();
}

bool Editor::initialize(GLFWwindow* window, const std::string& assetPath)
{
    if (m_isInitialized)
    {
        return true;
    }

    if (!window)
    {
        Logger::error("Editor::initialize — null window handle");
        return false;
    }

    m_window = window;
    m_assetPath = assetPath;

    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Do NOT enable ViewportsEnable — causes severe framerate drops on Linux/Mesa AMD

    // Load editor font (larger for accessibility — user is partially sighted).
    // Inter Tight chosen for cleaner FreeType rasterisation at small sizes
    // than Arimo (the previous default) per the Phase 9C design hand-off.
    std::string fontPath = assetPath + "/fonts/inter_tight.ttf";
    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f);
    if (!font)
    {
        Logger::warning("Editor: could not load font '" + fontPath
            + "' — using ImGui default font");
    }

    // Apply dark theme with accessibility adjustments
    setupTheme();

    // Initialize GLFW backend (true = install callbacks, chains with existing ones)
    ImGui_ImplGlfw_InitForOpenGL(window, true);

    // Initialize OpenGL backend (GLSL 4.50 matches our OpenGL 4.5 context)
    ImGui_ImplOpenGL3_Init("#version 450");

    // Create editor camera (orbit/pan/zoom for scene editing)
    m_editorCamera = std::make_unique<EditorCamera>();

    // Initialize the inspector panel (material preview, etc.)
    m_inspectorPanel.initialize(assetPath);

    // Initialize asset viewer panels
    m_textureViewerPanel.initialize(assetPath);
    m_hdriViewerPanel.initialize(assetPath);
    m_modelViewerPanel.initialize(assetPath);

    // Note: asset browser is initialized when setResourceManager() is called
    // because it needs a ResourceManager for texture loading.

    // Initialize file menu with the GLFW window for title bar updates
    m_fileMenu.setWindow(window);
    m_fileMenu.setCommandHistory(&m_commandHistory);

    // Wire command history to panels for undo support
    m_inspectorPanel.setCommandHistory(&m_commandHistory);
    m_hierarchyPanel.setCommandHistory(&m_commandHistory);

    // Initialize welcome panel (checks ~/.config/vestige/ for first-launch flag)
    std::string configDir;
    const char* home = std::getenv("HOME");
    if (home)
    {
        configDir = std::string(home) + "/.config/vestige";
    }
    else
    {
        configDir = "/tmp/vestige";
    }
    m_welcomePanel.initialize(configDir);

    // AUDIT.md §H16 / FIXPLAN H1: settings file is re-enabled now that
    // NodeEditorWidget routes Save/Load through an m_isShuttingDown-gated
    // callback — the library's own DestroyEditor save path was the SEGV
    // source. Runtime edits persist to this file; shutdown suppresses
    // the final save.
    const std::string nodeEditorSettingsPath = configDir + "/NodeEditor.json";
    // Populate the editor-owned registry with the same node palette the
    // runtime ScriptingSystem would, and hand it to the panel so pin
    // rendering + connection resolution works (without it, renderGraph
    // silently skips every pin and templates appear to do nothing).
    registerCoreNodeTypes(m_nodeTypeRegistry);
    registerEventNodeTypes(m_nodeTypeRegistry);
    registerActionNodeTypes(m_nodeTypeRegistry);
    registerPureNodeTypes(m_nodeTypeRegistry);
    registerFlowNodeTypes(m_nodeTypeRegistry);
    registerLatentNodeTypes(m_nodeTypeRegistry);
    m_scriptEditorPanel.setRegistry(&m_nodeTypeRegistry);
    m_scriptEditorPanel.initialize(nodeEditorSettingsPath);

    m_isInitialized = true;
    Logger::info("Editor initialized (ImGui + docking + editor camera)");
    return true;
}

void Editor::shutdown()
{
    if (!m_isInitialized)
    {
        return;
    }

    m_editorCamera.reset();

    // Cleanup asset viewer panels
    m_textureViewerPanel.cleanup();
    m_hdriViewerPanel.cleanup();
    m_modelViewerPanel.cleanup();

    // Shut down imgui-node-editor BEFORE the ImGui context is destroyed.
    // ed::DestroyEditor touches ImGui internals on teardown; if we let this
    // run from the panel's destructor after ImGui::DestroyContext, it reads
    // freed memory and SEGVs.
    m_scriptEditorPanel.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_isInitialized = false;
    m_window = nullptr;
    Logger::info("Editor shut down");
}

void Editor::wireFirstRunWizard(OnboardingSettings* onboarding,
                                 std::filesystem::path assetRoot,
                                 std::function<void()> applyDemoCallback)
{
    m_firstRunWizard.initialize(onboarding, std::move(assetRoot));
    m_applyDemoCallback = std::move(applyDemoCallback);
    m_wizardWasOpenLastFrame = m_firstRunWizard.isOpen();
    m_wizardJustClosedThisFrame = false;
}

bool Editor::consumeWizardJustClosed()
{
    const bool v = m_wizardJustClosedThisFrame;
    m_wizardJustClosedThisFrame = false;
    return v;
}

void Editor::wireSettingsEditorPanel(SettingsEditor* editor,
                                      InputActionMap* inputMap,
                                      std::filesystem::path settingsPath)
{
    m_settingsEditorPanel.initialize(editor, inputMap, std::move(settingsPath));
}

void Editor::prepareFrame()
{
    if (!m_isInitialized)
    {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void Editor::drawPanels(Renderer* renderer, Scene* scene, Camera* camera,
                        Timer* timer, Window* window)
{
    if (!m_isInitialized)
    {
        return;
    }

    if (m_mode == EditorMode::EDIT)
    {
        // Full-window dockspace
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiID dockspaceId = ImGui::GetID("VestigeDockSpace");

        // Build default layout on first run
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
        {
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID main = dockspaceId;
            ImGuiID left = 0;
            ImGuiID right = 0;
            ImGuiID bottom = 0;

            ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.18f, &left, &main);
            ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.28f, &right, &main);
            ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.25f, &bottom, &main);

            ImGui::DockBuilderDockWindow("Hierarchy", left);
            ImGui::DockBuilderDockWindow("Inspector", right);
            ImGui::DockBuilderDockWindow("Environment", right);
            ImGui::DockBuilderDockWindow("Console", bottom);
            ImGui::DockBuilderDockWindow("Assets", bottom);
            ImGui::DockBuilderDockWindow("History", bottom);
            ImGui::DockBuilderDockWindow("Model Viewer", bottom);
            ImGui::DockBuilderDockWindow("Texture Viewer", bottom);
            ImGui::DockBuilderDockWindow("HDRI Viewer", bottom);
            ImGui::DockBuilderDockWindow("Viewport", main);

            ImGui::DockBuilderFinish(dockspaceId);
        }

        ImGui::DockSpaceOverViewport(dockspaceId, viewport);

        // Process global file shortcuts (Ctrl+N/O/S/Shift+S/Q)
        m_fileMenu.processShortcuts(scene, m_selection);

        // Process undo/redo shortcuts (Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z)
        {
            const ImGuiIO& undoIo = ImGui::GetIO();
            if (!ImGui::IsPopupOpen("Unsaved Changes"))
            {
                if (undoIo.KeyCtrl && !undoIo.KeyShift
                    && ImGui::IsKeyPressed(ImGuiKey_Z))
                {
                    m_commandHistory.undo();
                }
                if (undoIo.KeyCtrl && !undoIo.KeyShift
                    && ImGui::IsKeyPressed(ImGuiKey_Y))
                {
                    m_commandHistory.redo();
                }
                if (undoIo.KeyCtrl && undoIo.KeyShift
                    && ImGui::IsKeyPressed(ImGuiKey_Z))
                {
                    m_commandHistory.redo();
                }
            }
        }

        // Menu bar
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                m_fileMenu.drawMenuItems(scene, m_selection);
                ImGui::Separator();
                if (ImGui::MenuItem("Import Model...", "Ctrl+I"))
                {
                    m_importDialog.open();
                }
                if (ImGui::MenuItem("New from Template..."))
                {
                    m_templateDialog.open();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                {
                    m_fileMenu.requestQuit();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_commandHistory.canUndo()))
                {
                    m_commandHistory.undo();
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_commandHistory.canRedo()))
                {
                    m_commandHistory.redo();
                }
                ImGui::Separator();

                bool hasSel = m_selection.hasSelection();
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSel && scene))
                {
                    const Entity* clone = EntityActions::duplicateEntity(
                        *scene, m_selection, m_selection.getPrimaryId());
                    if (clone)
                    {
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, clone->getId()));
                    }
                }
                if (ImGui::MenuItem("Delete", "Del", false, hasSel && scene))
                {
                    std::vector<uint32_t> ids = m_selection.getSelectedIds();
                    m_selection.clearSelection();
                    if (ids.size() == 1)
                    {
                        m_commandHistory.execute(
                            std::make_unique<DeleteEntityCommand>(*scene, ids[0]));
                    }
                    else if (ids.size() > 1)
                    {
                        std::vector<std::unique_ptr<EditorCommand>> cmds;
                        for (uint32_t id : ids)
                        {
                            cmds.push_back(std::make_unique<DeleteEntityCommand>(*scene, id));
                        }
                        m_commandHistory.execute(
                            std::make_unique<CompositeCommand>(
                                "Delete " + std::to_string(ids.size()) + " entities",
                                std::move(cmds)));
                    }
                }
                if (ImGui::MenuItem("Group", "Ctrl+G", false, hasSel && scene))
                {
                    EntityActions::groupEntities(*scene, m_selection);
                    m_fileMenu.markDirty();
                }
                if (ImGui::MenuItem("Toggle Visibility", "H", false, hasSel && scene))
                {
                    const Entity* entity = scene->findEntityById(m_selection.getPrimaryId());
                    if (entity)
                    {
                        bool oldVis = entity->isVisible();
                        m_commandHistory.execute(
                            std::make_unique<EntityPropertyCommand>(
                                *scene, entity->getId(),
                                EntityProperty::VISIBLE, oldVis, !oldVis));
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy Transform", "Ctrl+Shift+C", false, hasSel && scene))
                {
                    EntityActions::copyTransform(
                        *scene, m_selection.getPrimaryId(), m_transformClipboard);
                }
                if (ImGui::MenuItem("Paste Transform", "Ctrl+Shift+V", false, hasSel && scene))
                {
                    EntityActions::pasteTransform(
                        *scene, m_selection.getPrimaryId(), m_transformClipboard);
                }

                ImGui::Separator();
                bool hasMultiSel = hasSel && m_selection.getSelectedIds().size() >= 2;
                bool hasTriSel = hasSel && m_selection.getSelectedIds().size() >= 3;

                if (ImGui::BeginMenu("Align", hasMultiSel && scene))
                {
                    if (ImGui::MenuItem("Align Left (X Min)"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::X, EntityActions::AlignAnchor::MIN);
                    }
                    if (ImGui::MenuItem("Align Right (X Max)"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::X, EntityActions::AlignAnchor::MAX);
                    }
                    if (ImGui::MenuItem("Align Center X"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::X, EntityActions::AlignAnchor::CENTER);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Align Bottom (Y Min)"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::Y, EntityActions::AlignAnchor::MIN);
                    }
                    if (ImGui::MenuItem("Align Top (Y Max)"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::Y, EntityActions::AlignAnchor::MAX);
                    }
                    if (ImGui::MenuItem("Align Center Y"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::Y, EntityActions::AlignAnchor::CENTER);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Align Front (Z Min)"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::Z, EntityActions::AlignAnchor::MIN);
                    }
                    if (ImGui::MenuItem("Align Back (Z Max)"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::Z, EntityActions::AlignAnchor::MAX);
                    }
                    if (ImGui::MenuItem("Align Center Z"))
                    {
                        EntityActions::alignEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::Z, EntityActions::AlignAnchor::CENTER);
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Distribute", hasTriSel && scene))
                {
                    if (ImGui::MenuItem("Distribute X"))
                    {
                        EntityActions::distributeEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::X);
                    }
                    if (ImGui::MenuItem("Distribute Y"))
                    {
                        EntityActions::distributeEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::Y);
                    }
                    if (ImGui::MenuItem("Distribute Z"))
                    {
                        EntityActions::distributeEntities(*scene, m_selection, m_commandHistory,
                            EntityActions::AlignAxis::Z);
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                // Camera presets
                bool hasEditorCam = m_editorCamera != nullptr;
                if (ImGui::MenuItem("Front View", "Numpad 1", false, hasEditorCam))
                {
                    m_editorCamera->setFrontView();
                }
                if (ImGui::MenuItem("Right View", "Numpad 3", false, hasEditorCam))
                {
                    m_editorCamera->setRightView();
                }
                if (ImGui::MenuItem("Top View", "Numpad 7", false, hasEditorCam))
                {
                    m_editorCamera->setTopView();
                }
                if (ImGui::MenuItem("Focus Selection", "F", false, hasEditorCam && scene))
                {
                    const Entity* selected = m_selection.hasSelection()
                        ? m_selection.getPrimaryEntity(*scene) : nullptr;
                    if (selected)
                    {
                        m_editorCamera->focusOn(selected->getWorldPosition());
                    }
                    else
                    {
                        m_editorCamera->focusOn(glm::vec3(0.0f, 0.5f, 0.0f));
                    }
                }
                ImGui::Separator();

                // Viewport overlays
                if (ImGui::MenuItem("Ground Grid", "G", m_showGrid))
                {
                    m_showGrid = !m_showGrid;
                }
                if (ImGui::MenuItem("All Light Gizmos", nullptr, m_showAllLightGizmos))
                {
                    m_showAllLightGizmos = !m_showAllLightGizmos;
                }

                ImGui::Separator();

                // Panel toggles
                bool envOpen = m_environmentPanel.isOpen();
                if (ImGui::MenuItem("Environment", nullptr, &envOpen))
                {
                    m_environmentPanel.setOpen(envOpen);
                }
                bool terrainOpen = m_terrainPanel.isOpen();
                if (ImGui::MenuItem("Terrain", nullptr, &terrainOpen))
                {
                    m_terrainPanel.setOpen(terrainOpen);
                }
                bool navOpen = m_navigationPanel.isOpen();
                if (ImGui::MenuItem("Navigation", nullptr, &navOpen))
                {
                    m_navigationPanel.setOpen(navOpen);
                }
                bool uiLayoutOpen = m_uiLayoutPanel.isOpen();
                if (ImGui::MenuItem("UI Layout", nullptr, &uiLayoutOpen))
                {
                    m_uiLayoutPanel.setOpen(uiLayoutOpen);
                }
                bool uiRuntimeOpen = m_uiRuntimePanel.isOpen();
                if (ImGui::MenuItem("UI Runtime", nullptr, &uiRuntimeOpen))
                {
                    m_uiRuntimePanel.setOpen(uiRuntimeOpen);
                }
                ImGui::MenuItem("Console", nullptr, &m_showConsole);
                ImGui::MenuItem("Statistics", nullptr, &m_showStatistics);
                {
                    bool scriptOpen = m_scriptEditorPanel.isOpen();
                    if (ImGui::MenuItem("Script Editor", nullptr, &scriptOpen))
                    {
                        if (scriptOpen) m_scriptEditorPanel.open();
                        else            m_scriptEditorPanel.close();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Capture Screenshot", "F11"))
                {
                    m_captureScreenshotRequested = true;
                }
                if (ImGui::MenuItem("Fullscreen Viewport", "Ctrl+Shift+F", m_fullscreenViewport))
                {
                    m_fullscreenViewport = !m_fullscreenViewport;
                }
                bool perfOpen = m_performancePanel.isOpen();
                if (ImGui::MenuItem("Performance", "F12", &perfOpen))
                {
                    m_performancePanel.setOpen(perfOpen);
                }
                bool valOpen = m_validationPanel.isOpen();
                if (ImGui::MenuItem("Scene Validation", nullptr, &valOpen))
                {
                    m_validationPanel.setOpen(valOpen);
                }
                ImGui::Separator();
                ImGui::TextDisabled("Asset Viewers");
                bool modelViewerOpen = m_modelViewerPanel.isOpen();
                if (ImGui::MenuItem("Model Viewer", nullptr, &modelViewerOpen))
                {
                    m_modelViewerPanel.setOpen(modelViewerOpen);
                }
                bool texViewerOpen = m_textureViewerPanel.isOpen();
                if (ImGui::MenuItem("Texture Viewer", nullptr, &texViewerOpen))
                {
                    m_textureViewerPanel.setOpen(texViewerOpen);
                }
                bool hdriViewerOpen = m_hdriViewerPanel.isOpen();
                if (ImGui::MenuItem("HDRI Viewer", nullptr, &hdriViewerOpen))
                {
                    m_hdriViewerPanel.setOpen(hdriViewerOpen);
                }
                ImGui::Separator();
                ImGui::MenuItem("Demo Window", nullptr, &m_showDemoWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tools"))
            {
                bool isTranslate = (m_gizmoOperation == ImGuizmo::TRANSLATE);
                bool isRotate = (m_gizmoOperation == ImGuizmo::ROTATE);
                bool isScale = (m_gizmoOperation == ImGuizmo::SCALE);
                if (ImGui::MenuItem("Translate", "W", isTranslate))
                {
                    m_gizmoOperation = ImGuizmo::TRANSLATE;
                }
                if (ImGui::MenuItem("Rotate", "E", isRotate))
                {
                    m_gizmoOperation = ImGuizmo::ROTATE;
                }
                if (ImGui::MenuItem("Scale", "R", isScale))
                {
                    m_gizmoOperation = ImGuizmo::SCALE;
                }
                ImGui::Separator();
                bool isWorld = (m_gizmoMode == ImGuizmo::WORLD);
                if (ImGui::MenuItem("World Space", "L", isWorld))
                {
                    m_gizmoMode = ImGuizmo::WORLD;
                }
                if (ImGui::MenuItem("Local Space", "L", !isWorld))
                {
                    m_gizmoMode = ImGuizmo::LOCAL;
                }
                ImGui::Separator();
                ImGui::Text("Snap Settings");
                ImGui::SetNextItemWidth(100.0f);
                ImGui::DragFloat("Translation", &m_snapTranslation, 0.05f, 0.1f, 10.0f, "%.2f");
                ImGui::SetNextItemWidth(100.0f);
                ImGui::DragFloat("Rotation", &m_snapRotation, 1.0f, 1.0f, 90.0f, "%.0f deg");
                ImGui::SetNextItemWidth(100.0f);
                ImGui::DragFloat("Scale##snap", &m_snapScale, 0.01f, 0.01f, 1.0f, "%.2f");
                ImGui::Separator();
                if (ImGui::MenuItem("Ruler / Measure", nullptr, m_rulerTool.isActive()))
                {
                    if (m_rulerTool.isActive())
                    {
                        m_rulerTool.cancel();
                    }
                    else
                    {
                        m_rulerTool.startMeasurement();
                    }
                }
                if (m_rulerTool.hasMeasurement())
                {
                    ImGui::Text("  Distance: %.3f m", static_cast<double>(m_rulerTool.getDistance()));
                }

                ImGui::Separator();
                ImGui::TextDisabled("Architectural Tools");

                if (ImGui::MenuItem("Wall Tool", nullptr, m_wallTool.isActive()))
                {
                    if (m_wallTool.isActive()) m_wallTool.cancel();
                    else m_wallTool.activate();
                }
                if (m_wallTool.isActive())
                {
                    ImGui::DragFloat("  Wall Height", &m_wallTool.height, 0.1f, 0.5f, 20.0f, "%.1f m");
                    ImGui::DragFloat("  Wall Thickness", &m_wallTool.thickness, 0.01f, 0.05f, 1.0f, "%.2f m");
                }
                if (ImGui::MenuItem("Room Tool (Dimensions)"))
                {
                    m_roomTool.activateDimensionMode();
                }
                if (ImGui::MenuItem("Room Tool (Click)"))
                {
                    m_roomTool.activateClickMode();
                }
                if (ImGui::MenuItem("Cutout Tool (Door/Window)", nullptr, m_cutoutTool.isActive()))
                {
                    if (m_cutoutTool.isActive()) m_cutoutTool.cancel();
                    else m_cutoutTool.activate();
                }
                if (ImGui::MenuItem("Roof Tool"))
                {
                    m_roofTool.activate();
                }
                if (ImGui::MenuItem("Stair Tool"))
                {
                    m_stairTool.activate();
                }

                ImGui::Separator();
                ImGui::TextDisabled("Path Tools");

                if (ImGui::MenuItem("Path / Road Tool", nullptr, m_pathTool.isActive()))
                {
                    if (m_pathTool.isActive()) m_pathTool.cancel();
                    else m_pathTool.activate();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Render"))
            {
                if (renderer)
                {
                    bool wireframe = renderer->isWireframeMode();
                    if (ImGui::MenuItem("Wireframe", "F1", wireframe))
                    {
                        renderer->setWireframeMode(!wireframe);
                    }
                    ImGui::Separator();

                    // Tonemapping
                    int tonemapMode = renderer->getTonemapMode();
                    if (ImGui::BeginMenu("Tonemapping (F2)"))
                    {
                        const char* tonemapNames[] = {"Reinhard", "ACES Filmic", "None (linear)"};
                        for (int i = 0; i < 3; ++i)
                        {
                            if (ImGui::MenuItem(tonemapNames[i], nullptr, tonemapMode == i))
                            {
                                renderer->setTonemapMode(i);
                            }
                        }
                        ImGui::EndMenu();
                    }

                    bool autoExp = renderer->isAutoExposure();
                    if (ImGui::MenuItem("Auto-Exposure", "F10", autoExp))
                    {
                        renderer->setAutoExposure(!autoExp);
                    }

                    bool hdrDebug = renderer->getDebugMode() != 0;
                    if (ImGui::MenuItem("HDR Debug", "F3", hdrDebug))
                    {
                        renderer->setDebugMode(hdrDebug ? 0 : 1);
                    }
                    ImGui::Separator();

                    bool pom = renderer->isPomEnabled();
                    if (ImGui::MenuItem("Parallax Mapping", "F4", pom))
                    {
                        renderer->setPomEnabled(!pom);
                    }

                    bool bloom = renderer->isBloomEnabled();
                    if (ImGui::MenuItem("Bloom", "F5", bloom))
                    {
                        renderer->setBloomEnabled(!bloom);
                    }

                    bool ssao = renderer->isSsaoEnabled();
                    if (ImGui::MenuItem("SSAO", "F6", ssao))
                    {
                        renderer->setSsaoEnabled(!ssao);
                    }

                    // Anti-aliasing
                    AntiAliasMode aaMode = renderer->getAntiAliasMode();
                    if (ImGui::BeginMenu("Anti-Aliasing (F7)"))
                    {
                        const char* aaNames[] = {"None", "MSAA 4x", "TAA", "SMAA"};
                        for (int i = 0; i < 4; ++i)
                        {
                            if (ImGui::MenuItem(aaNames[i], nullptr,
                                                static_cast<int>(aaMode) == i))
                            {
                                renderer->setAntiAliasMode(static_cast<AntiAliasMode>(i));
                            }
                        }
                        ImGui::EndMenu();
                    }

                    bool colorGrading = renderer->isColorGradingEnabled();
                    if (ImGui::MenuItem("Color Grading", "F8", colorGrading))
                    {
                        if (colorGrading)
                        {
                            renderer->setColorGradingEnabled(false);
                        }
                        else
                        {
                            renderer->setColorGradingEnabled(true);
                            renderer->nextColorGradingPreset();
                        }
                    }

                    ImGui::Separator();
                    bool csmDebug = renderer->isCascadeDebug();
                    if (ImGui::MenuItem("CSM Debug", "F9", csmDebug))
                    {
                        renderer->setCascadeDebug(!csmDebug);
                    }
                }
                else
                {
                    ImGui::TextDisabled("Renderer not available");
                }

                ImGui::Separator();
                ImGui::Text("Play Mode Resolution");
                struct ResOption { const char* label; int w; int h; };
                static const ResOption resOptions[] = {
                    {"1280 x 720  (720p)",  1280,  720},
                    {"1600 x 900",          1600,  900},
                    {"1920 x 1080 (1080p)", 1920, 1080},
                    {"2560 x 1440 (1440p)", 2560, 1440},
                    {"3840 x 2160 (4K)",    3840, 2160},
                };
                for (const auto& opt : resOptions)
                {
                    bool selected = (m_playModeWidth == opt.w && m_playModeHeight == opt.h);
                    if (ImGui::MenuItem(opt.label, nullptr, selected))
                    {
                        m_playModeWidth = opt.w;
                        m_playModeHeight = opt.h;
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Create"))
            {
                // Spawn position = editor camera's focus point (where camera is looking at)
                glm::vec3 spawnPos = m_editorCamera
                    ? m_editorCamera->getFocusPoint() : glm::vec3(0.0f);

                if (ImGui::MenuItem("Empty Entity"))
                {
                    if (scene)
                    {
                        Entity* entity = EntityFactory::createEmptyEntity(*scene, spawnPos);
                        m_selection.select(entity->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, entity->getId()));
                    }
                }
                ImGui::Separator();

                bool canSpawnPrimitive = scene && m_resourceManager;
                if (ImGui::BeginMenu("Primitives"))
                {
                    if (ImGui::MenuItem("Cube", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createCube(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Sphere", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createSphere(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Plane", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createPlane(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Cylinder", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createCylinder(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Cone", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createCone(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Wedge", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createWedge(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Architecture"))
                {
                    if (ImGui::MenuItem("Wall", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createWall(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Wall with Door", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createWallWithDoor(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Wall with Window", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createWallWithWindow(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Room (4x4x3m)", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createRoom(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Floor Slab", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createFloorSlab(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Roof (Gable)", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createRoof(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Stairs", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createStairs(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Spiral Stairs", nullptr, false, canSpawnPrimitive))
                    {
                        Entity* e = EntityFactory::createSpiralStairs(*scene, *m_resourceManager, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Prefabs"))
                {
                    bool canSpawnPrefab = scene && m_resourceManager;
                    auto prefabFiles = m_prefabSystem.listPrefabs(m_assetPath);

                    if (prefabFiles.empty())
                    {
                        ImGui::TextDisabled("No prefabs saved yet");
                    }
                    else
                    {
                        for (const auto& prefabFile : prefabFiles)
                        {
                            std::filesystem::path p(prefabFile);
                            std::string displayName = p.stem().string();

                            if (ImGui::MenuItem(displayName.c_str(), nullptr,
                                                false, canSpawnPrefab))
                            {
                                Entity* e = m_prefabSystem.loadPrefab(
                                    prefabFile, *scene, *m_resourceManager);
                                if (e)
                                {
                                    e->transform.position = spawnPos;
                                    m_selection.select(e->getId());
                                    m_commandHistory.execute(
                                        std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                                }
                            }
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Lights"))
                {
                    bool canSpawnLight = scene != nullptr;
                    if (ImGui::MenuItem("Directional Light", nullptr, false, canSpawnLight))
                    {
                        Entity* e = EntityFactory::createDirectionalLight(*scene, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Point Light", nullptr, false, canSpawnLight))
                    {
                        Entity* e = EntityFactory::createPointLight(*scene, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::MenuItem("Spot Light", nullptr, false, canSpawnLight))
                    {
                        Entity* e = EntityFactory::createSpotLight(*scene, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Effects"))
                {
                    bool canSpawnEffect = scene != nullptr;
                    if (ImGui::MenuItem("Particle Emitter", nullptr, false, canSpawnEffect))
                    {
                        Entity* e = EntityFactory::createParticleEmitter(*scene, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    if (ImGui::BeginMenu("Particle Presets", canSpawnEffect))
                    {
                        const char* presetNames[] = {"torch", "candle", "campfire", "smoke", "dust", "incense", "sparks"};
                        const char* presetLabels[] = {"Torch Fire", "Candle Flame", "Campfire", "Smoke", "Dust Motes", "Incense Smoke", "Sparks"};
                        for (int i = 0; i < 7; ++i)
                        {
                            if (ImGui::MenuItem(presetLabels[i]))
                            {
                                Entity* e = EntityFactory::createParticlePreset(*scene, spawnPos, presetNames[i]);
                                m_selection.select(e->getId());
                                m_commandHistory.execute(
                                    std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                            }
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Water Surface", nullptr, false, canSpawnEffect))
                    {
                        Entity* e = EntityFactory::createWaterSurface(*scene, spawnPos);
                        m_selection.select(e->getId());
                        m_commandHistory.execute(
                            std::make_unique<CreateEntityCommand>(*scene, e->getId()));
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Controls Reference"))
                {
                    m_showControlsWindow = true;
                }
                if (ImGui::MenuItem("Welcome Screen"))
                {
                    m_welcomePanel.open();
                }
                if (ImGui::MenuItem("Settings..."))
                {
                    m_settingsEditorPanel.open();
                }
                if (ImGui::MenuItem("First-Run Wizard"))
                {
                    m_firstRunWizard.openFromHelpMenu();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // --- Viewport panel: display the rendered scene as a texture ---
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");
        {
            ImVec2 contentMin = ImGui::GetCursorScreenPos();
            ImVec2 viewportSize = ImGui::GetContentRegionAvail();

            // Store viewport bounds for click detection (used next frame in processViewportClick)
            m_viewportMin = glm::vec2(contentMin.x, contentMin.y);
            m_viewportMax = glm::vec2(contentMin.x + viewportSize.x, contentMin.y + viewportSize.y);

            if (viewportSize.x > 0 && viewportSize.y > 0 && renderer)
            {
                GLuint texId = renderer->getOutputTextureId();
                if (texId != 0)
                {
                    // UV flipped vertically: OpenGL textures are bottom-up, ImGui expects top-down
                    ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(texId)),
                        viewportSize, ImVec2(0, 1), ImVec2(1, 0));
                }
            }

            // Draw transform gizmo overlay on the viewport
            drawGizmo(camera, scene);

            // Draw gizmo mode/operation indicator
            drawGizmoOverlay();

            // Process W/E/R/L gizmo keyboard shortcuts
            processGizmoShortcuts();

            // Process Delete, Ctrl+D, Ctrl+Shift+C/V entity shortcuts
            processEntityShortcuts(scene);

            // Draw box selection rectangle overlay
            if (m_boxSelectActive)
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p0(std::min(m_boxSelectStart.x, m_boxSelectEnd.x),
                          std::min(m_boxSelectStart.y, m_boxSelectEnd.y));
                ImVec2 p1(std::max(m_boxSelectStart.x, m_boxSelectEnd.x),
                          std::max(m_boxSelectStart.y, m_boxSelectEnd.y));
                dl->AddRectFilled(p0, p1, IM_COL32(60, 120, 220, 40));
                dl->AddRect(p0, p1, IM_COL32(60, 120, 220, 180), 0.0f, 0, 1.5f);
            }

            m_viewportFocused = ImGui::IsWindowFocused();
            m_viewportHovered = ImGui::IsWindowHovered();
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // --- Side panels (hidden in fullscreen viewport mode) ---
        if (!m_fullscreenViewport)
        {

        // --- Hierarchy panel ---
        ImGui::Begin("Hierarchy");
        m_hierarchyPanel.draw(scene, m_selection);
        ImGui::End();

        // Process pending "Save as Prefab" from hierarchy context menu
        if (m_hierarchyPanel.hasPendingSavePrefab() && scene && m_resourceManager)
        {
            const Entity* entity = scene->findEntityById(
                m_hierarchyPanel.getPendingSavePrefabEntityId());
            if (entity)
            {
                m_prefabSystem.savePrefab(
                    *entity, m_hierarchyPanel.getPendingSavePrefabName(),
                    *m_resourceManager, m_assetPath);
            }
            m_hierarchyPanel.clearPendingSavePrefab();
        }

        // --- Inspector panel ---
        ImGui::SetNextWindowSizeConstraints(ImVec2(280, 200), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::Begin("Inspector");
        m_inspectorPanel.draw(scene, m_selection);
        ImGui::End();

        if (m_showConsole)
        {
        ImGui::Begin("Console", &m_showConsole);
        {
            // Filter buttons
            static bool showTrace = false;
            static bool showDebug = false;
            static bool showInfo = true;
            static bool showWarn = true;
            static bool showError = true;

            ImGui::Checkbox("Trace", &showTrace); ImGui::SameLine();
            ImGui::Checkbox("Debug", &showDebug); ImGui::SameLine();
            ImGui::Checkbox("Info",  &showInfo);  ImGui::SameLine();
            ImGui::Checkbox("Warn",  &showWarn);  ImGui::SameLine();
            ImGui::Checkbox("Error", &showError); ImGui::SameLine();

            if (ImGui::Button("Clear"))
            {
                Logger::clearEntries();
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy"))
            {
                // Build filtered log text and copy to clipboard
                std::string logText;
                for (const auto& e : Logger::getEntries())
                {
                    bool include = false;
                    switch (e.level)
                    {
                        case LogLevel::Trace:   include = showTrace; break;
                        case LogLevel::Debug:   include = showDebug; break;
                        case LogLevel::Info:    include = showInfo;  break;
                        case LogLevel::Warning: include = showWarn;  break;
                        case LogLevel::Error:
                        case LogLevel::Fatal:   include = showError; break;
                    }
                    if (include)
                    {
                        logText += "[";
                        logText += Logger::levelToString(e.level);
                        logText += "] ";
                        logText += e.message;
                        logText += "\n";
                    }
                }
                ImGui::SetClipboardText(logText.c_str());
            }
            ImGui::Separator();

            // Scrollable log region — selectable text for individual line copying
            ImGui::BeginChild("LogScroll", ImVec2(0, 0), false,
                              ImGuiWindowFlags_HorizontalScrollbar);

            const auto& entries = Logger::getEntries();
            for (const auto& entry : entries)
            {
                // Filter by level
                switch (entry.level)
                {
                    case LogLevel::Trace:   if (!showTrace) continue; break;
                    case LogLevel::Debug:   if (!showDebug) continue; break;
                    case LogLevel::Info:    if (!showInfo)  continue; break;
                    case LogLevel::Warning: if (!showWarn)  continue; break;
                    case LogLevel::Error:
                    case LogLevel::Fatal:   if (!showError) continue; break;
                }

                // Color by severity
                ImVec4 color;
                switch (entry.level)
                {
                    case LogLevel::Trace:   color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
                    case LogLevel::Debug:   color = ImVec4(0.6f, 0.6f, 0.8f, 1.0f); break;
                    case LogLevel::Info:    color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); break;
                    case LogLevel::Warning: color = ImVec4(1.0f, 0.85f, 0.2f, 1.0f); break;
                    case LogLevel::Error:   color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break;
                    case LogLevel::Fatal:   color = ImVec4(1.0f, 0.1f, 0.1f, 1.0f); break;
                }
                std::string lineText = "[" + std::string(Logger::levelToString(entry.level))
                                     + "] " + entry.message;
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.4f, 0.4f));
                if (ImGui::Selectable(lineText.c_str(), false,
                                      ImGuiSelectableFlags_AllowDoubleClick))
                {
                    // Single click: copy this line to clipboard
                    ImGui::SetClipboardText(lineText.c_str());
                }
                ImGui::PopStyleColor(2);
            }

            // Auto-scroll to bottom when new messages arrive
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
            {
                ImGui::SetScrollHereY(1.0f);
            }

            ImGui::EndChild();
        }
        ImGui::End();
        } // m_showConsole

        // --- Asset browser panel ---
        ImGui::Begin("Assets");
        m_assetBrowserPanel.draw();
        ImGui::End();

        // --- History panel ---
        ImGui::Begin("History");
        m_historyPanel.draw(m_commandHistory);
        ImGui::End();

        // --- Environment painting panel ---
        if (m_foliageManager)
        {
            m_environmentPanel.draw(m_brushTool, *m_foliageManager, m_commandHistory, m_terrain);
        }

        // --- Terrain panel ---
        if (m_terrain)
        {
            m_terrainPanel.draw(m_terrainBrush, *m_terrain, m_commandHistory);
        }

        // --- Navigation panel ---
        m_navigationPanel.draw(m_navigationSystem, scene);

        // --- Audio panel ---
        m_audioPanel.draw(m_audioSystem, scene);

        // --- UI Layout panel ---
        // Engine doesn't own a single "the" canvas — game projects attach
        // their own. The panel is still useful for inspecting an empty state
        // + live-editing the theme. Game code can push a specific canvas in
        // via uiLayoutPanel.draw(&canvas, &theme) outside the editor loop.
        m_uiLayoutPanel.draw(/*canvas=*/nullptr, /*theme=*/nullptr);

        // --- UI Runtime panel ---
        // Uses the engine's live UISystem when it has been injected via
        // `setUISystem`; otherwise the panel renders an empty-state
        // banner in each tab.
        m_uiRuntimePanel.draw(m_uiSystem);

        // --- Performance panel ---
        if (m_profiler)
        {
            m_performancePanel.draw(*m_profiler, renderer, timer, window);
        }

        // --- Validation panel ---
        m_validationPanel.draw(scene, m_selection);

        // --- Asset viewer panels ---
        m_textureViewerPanel.draw();
        m_hdriViewerPanel.draw(renderer);
        float dt = timer ? timer->getDeltaTime() : (1.0f / 60.0f);
        m_modelViewerPanel.draw(scene, m_resourceManager, dt);

        // --- Template dialog ---
        m_templateDialog.draw(scene, m_resourceManager, renderer,
                               m_selection, m_fileMenu);

        // --- First-run wizard (Phase 10.5 slice 14.4) ---
        // Runs after the TemplateDialog draw so the two modals don't
        // fight for popup ID. The wizard only renders when opened
        // (auto on first launch, or on-demand from Help → First-Run
        // Wizard). Dispatches the chosen SceneOp here since Editor
        // owns scene + resources + renderer + the applyDemo callback.
        if (scene && m_resourceManager)
        {
            const bool wasOpen = m_firstRunWizard.isOpen();
            FirstRunWizardSceneOp op = m_firstRunWizard.draw();
            switch (op)
            {
                case FirstRunWizardSceneOp::None:
                    break;
                case FirstRunWizardSceneOp::ApplyEmpty:
                    applyEmptyScene(*scene, *m_resourceManager);
                    m_fileMenu.markDirty();
                    m_selection.clearSelection();
                    break;
                case FirstRunWizardSceneOp::ApplyDemo:
                    if (m_applyDemoCallback)
                    {
                        m_applyDemoCallback();
                        m_fileMenu.markDirty();
                        m_selection.clearSelection();
                    }
                    break;
                case FirstRunWizardSceneOp::ApplyTemplate:
                {
                    const auto all = allWizardTemplates();
                    const int idx  = m_firstRunWizard.selectedTemplateIndex();
                    if (idx >= 0 && idx < static_cast<int>(all.size()))
                    {
                        TemplateDialog::applyTemplate(
                            all[static_cast<size_t>(idx)], scene,
                            m_resourceManager, renderer);
                        m_fileMenu.markDirty();
                        m_selection.clearSelection();
                    }
                    break;
                }
            }

            // Edge-trigger the "wizard just closed" flag so the engine
            // layer can save Settings on the next poll.
            const bool isOpen = m_firstRunWizard.isOpen();
            if (wasOpen && !isOpen)
            {
                m_wizardJustClosedThisFrame = true;
            }
            m_wizardWasOpenLastFrame = isOpen;
        }

        // --- Asset browser double-click routing ---
        if (m_assetBrowserPanel.hasPendingOpen())
        {
            std::string pendingPath;
            AssetType pendingType;
            m_assetBrowserPanel.consumePendingOpen(pendingPath, pendingType);

            if (pendingType == AssetType::TEXTURE)
            {
                auto tex = std::make_shared<Texture>();
                std::string ext = std::filesystem::path(pendingPath).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                bool isHdr = (ext == ".hdr" || ext == ".exr");

                if (isHdr)
                {
                    m_hdriViewerPanel.openHdri(pendingPath, m_resourceManager);
                }
                else
                {
                    bool linear = false;
                    // Detect linear textures by common suffixes
                    std::string stem = std::filesystem::path(pendingPath).stem().string();
                    std::string lowerStem = stem;
                    std::transform(lowerStem.begin(), lowerStem.end(), lowerStem.begin(), ::tolower);
                    if (lowerStem.find("normal") != std::string::npos ||
                        lowerStem.find("roughness") != std::string::npos ||
                        lowerStem.find("metallic") != std::string::npos ||
                        lowerStem.find("ao") != std::string::npos ||
                        lowerStem.find("height") != std::string::npos)
                    {
                        linear = true;
                    }

                    if (tex->loadFromFile(pendingPath, linear))
                    {
                        m_textureViewerPanel.openTexture(tex, pendingPath);
                    }
                }
            }
            else if (pendingType == AssetType::MESH && m_resourceManager)
            {
                auto model = m_resourceManager->loadModel(pendingPath);
                if (model)
                {
                    m_modelViewerPanel.openModel(model, pendingPath);
                }
            }
        }

        // --- Welcome panel ---
        m_welcomePanel.draw();

        // --- Settings editor panel (Phase 10 slice 13.5b) ---
        m_settingsEditorPanel.draw();

        // --- Script editor panel (Phase 9E-3) ---
        m_scriptEditorPanel.draw();

        // --- Scene statistics panel ---
        if (m_showStatistics && scene)
        {
            ImGui::Begin("Statistics", &m_showStatistics);

            // Count entities by traversal
            int entityCount = 0;
            int meshCount = 0;
            int lightCount = 0;
            std::function<void(const Entity*)> countEntities = [&](const Entity* e)
            {
                if (!e) return;
                entityCount++;
                if (e->getComponent<MeshRenderer>()) meshCount++;
                if (e->getComponent<DirectionalLightComponent>() ||
                    e->getComponent<PointLightComponent>() ||
                    e->getComponent<SpotLightComponent>())
                    lightCount++;
                for (const auto& child : e->getChildren())
                    countEntities(child.get());
            };
            countEntities(scene->getRoot());

            ImGui::Text("Scene: %s", scene->getName().c_str());
            ImGui::Separator();
            ImGui::Text("Entities:  %d", entityCount);
            ImGui::Text("Meshes:    %d", meshCount);
            ImGui::Text("Lights:    %d", lightCount);

            if (renderer)
            {
                const auto& stats = renderer->getCullingStats();
                ImGui::Separator();
                ImGui::Text("Draw calls:      %d  (%d instanced)", stats.drawCalls, stats.instanceBatches);
                ImGui::Text("Opaque objects:  %d / %d visible", stats.culledItems, stats.totalItems);
                ImGui::Text("Transparent:     %d / %d visible", stats.transparentCulled, stats.transparentTotal);
                ImGui::Text("Shadow casters:  %d total", stats.shadowCastersTotal);
            }

            ImGui::End();
        }

        } // !m_fullscreenViewport (side panels)

        // --- Import dialog (file browser + settings modal) ---
        m_importDialog.draw(scene, m_resourceManager, m_selection,
                            m_editorCamera.get());

        // File browser dialogs and unsaved changes modal
        m_fileMenu.drawDialogs(scene, m_selection);

        // Demo window for testing ImGui features
        if (m_showDemoWindow)
        {
            ImGui::ShowDemoWindow(&m_showDemoWindow);
        }

        // --- Controls reference window ---
        if (m_showControlsWindow)
        {
            ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Controls Reference", &m_showControlsWindow))
            {
                auto shortcutRow = [](const char* key, const char* desc)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(key);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(desc);
                };

                // File
                if (ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("##file", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();
                        shortcutRow("Ctrl+N", "New scene");
                        shortcutRow("Ctrl+O", "Open scene");
                        shortcutRow("Ctrl+S", "Save scene");
                        shortcutRow("Ctrl+Shift+S", "Save scene as...");
                        shortcutRow("Ctrl+I", "Import model");
                        shortcutRow("Ctrl+Q", "Quit");
                        ImGui::EndTable();
                    }
                }

                // Edit
                if (ImGui::CollapsingHeader("Edit", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("##edit", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();
                        shortcutRow("Ctrl+Z", "Undo");
                        shortcutRow("Ctrl+Y / Ctrl+Shift+Z", "Redo");
                        shortcutRow("Delete", "Delete selected entity");
                        shortcutRow("Ctrl+D", "Duplicate selected entity");
                        shortcutRow("Ctrl+G", "Group selected entities");
                        shortcutRow("H", "Toggle visibility");
                        shortcutRow("F2", "Rename entity (in Hierarchy)");
                        shortcutRow("Ctrl+Shift+C", "Copy transform");
                        shortcutRow("Ctrl+Shift+V", "Paste transform");
                        ImGui::EndTable();
                    }
                }

                // Tools / Gizmo
                if (ImGui::CollapsingHeader("Tools / Gizmo", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("##tools", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();
                        shortcutRow("W", "Translate gizmo");
                        shortcutRow("E", "Rotate gizmo");
                        shortcutRow("R", "Scale gizmo");
                        shortcutRow("L", "Toggle World / Local space");
                        shortcutRow("Ctrl (hold)", "Enable snap while dragging gizmo");
                        ImGui::EndTable();
                    }
                }

                // Camera
                if (ImGui::CollapsingHeader("Camera (Edit Mode)", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("##camera", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();
                        shortcutRow("Alt + LMB drag", "Orbit around focus point");
                        shortcutRow("MMB drag", "Pan the view");
                        shortcutRow("Scroll wheel", "Zoom in / out");
                        shortcutRow("Numpad 1", "Front view");
                        shortcutRow("Numpad 3", "Right view");
                        shortcutRow("Numpad 7", "Top view");
                        shortcutRow("F", "Focus on selection (or scene center)");
                        ImGui::EndTable();
                    }
                }

                // Render
                if (ImGui::CollapsingHeader("Render Toggles"))
                {
                    if (ImGui::BeginTable("##render", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();
                        shortcutRow("F1", "Toggle wireframe");
                        shortcutRow("F2", "Cycle tonemapper (Reinhard / ACES / None)");
                        shortcutRow("F3", "Toggle HDR debug visualization");
                        shortcutRow("F4", "Toggle parallax occlusion mapping");
                        shortcutRow("F5", "Toggle bloom");
                        shortcutRow("F6", "Toggle SSAO");
                        shortcutRow("F7", "Cycle anti-aliasing (None / MSAA / TAA / SMAA)");
                        shortcutRow("F8", "Cycle color grading presets");
                        shortcutRow("F9", "Toggle cascade shadow map debug");
                        shortcutRow("F10", "Toggle auto-exposure");
                        shortcutRow("F11", "Capture frame diagnostics + screenshot");
                        shortcutRow("F12", "Toggle performance panel");
                        shortcutRow("G", "Toggle ground grid overlay");
                        shortcutRow("Ctrl+Shift+F", "Toggle fullscreen viewport");
                        shortcutRow("[ / ]", "Decrease / increase exposure (manual)");
                        shortcutRow("- / =", "Decrease / increase POM height");
                        ImGui::EndTable();
                    }
                }

                // Play Mode
                if (ImGui::CollapsingHeader("Play Mode (Escape to toggle)"))
                {
                    if (ImGui::BeginTable("##play", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();
                        shortcutRow("Escape", "Return to Edit mode");
                        shortcutRow("W / A / S / D", "Move forward / left / back / right");
                        shortcutRow("Space", "Jump");
                        shortcutRow("Left Ctrl", "Sprint");
                        shortcutRow("Mouse", "Look around");
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::End();
        }

        // Update window title to reflect current dirty state
        if (scene)
        {
            m_fileMenu.updateWindowTitle(scene->getName());
        }

        // Tick auto-save timer (writes autosave file every 120s when dirty)
        m_fileMenu.tickAutoSave(scene);
    }

    // Draw notification overlay (if active)
    if (m_notifyTimer > 0.0f)
    {
        float alpha = std::min(m_notifyTimer, 1.0f);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                        40.0f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.7f * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 8.0f));
        if (ImGui::Begin("##Notification", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                          | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                          | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove
                          | ImGuiWindowFlags_NoInputs))
        {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", m_notifyText.c_str());
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        m_notifyTimer -= ImGui::GetIO().DeltaTime;
    }

    // Draw ruler measurement overlay
    if (m_rulerTool.hasMeasurement())
    {
        ImGui::SetNextWindowPos(ImVec2(m_viewportMin.x + 10.0f,
                                        m_viewportMax.y - 40.0f));
        ImGui::SetNextWindowBgAlpha(0.7f);
        if (ImGui::Begin("##RulerOverlay", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                          | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                          | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove
                          | ImGuiWindowFlags_NoInputs))
        {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f),
                               "Ruler: %.3f m", static_cast<double>(m_rulerTool.getDistance()));
        }
        ImGui::End();
    }
    else if (m_rulerTool.isActive())
    {
        ImGui::SetNextWindowPos(ImVec2(m_viewportMin.x + 10.0f,
                                        m_viewportMax.y - 40.0f));
        ImGui::SetNextWindowBgAlpha(0.7f);
        if (ImGui::Begin("##RulerOverlay", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                          | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
                          | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove
                          | ImGuiWindowFlags_NoInputs))
        {
            const char* hint = (m_rulerTool.getState() == RulerTool::State::WAITING_A)
                                ? "Click to set point A"
                                : "Click to set point B";
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Ruler: %s", hint);
        }
        ImGui::End();
    }

    // Queue ruler debug draw lines
    m_rulerTool.queueDebugDraw();

    // Draw architectural tool dialogs
    if (m_roomTool.showingDialog())
    {
        m_roomTool.drawDimensionDialog(*scene, *m_resourceManager, m_commandHistory);
    }
    if (m_cutoutTool.showingDialog())
    {
        m_cutoutTool.drawConfigDialog(*scene, *m_resourceManager, m_commandHistory);
    }
    if (m_roofTool.showingPanel())
    {
        m_roofTool.drawConfigPanel();
    }
    if (m_stairTool.showingPanel())
    {
        m_stairTool.drawConfigPanel();
    }
    if (m_pathTool.showingPanel())
    {
        m_pathTool.drawConfigPanel();
    }
}

void Editor::endFrame()
{
    if (!m_isInitialized)
    {
        return;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Editor::updateEditorCamera(float deltaTime)
{
    if (!m_editorCamera)
    {
        return;
    }

    m_editorCamera->processInput(m_viewportHovered);
    m_editorCamera->update(deltaTime);
}

void Editor::applyEditorCamera(Camera& camera)
{
    if (!m_editorCamera)
    {
        return;
    }

    m_editorCamera->applyToCamera(camera);
}

EditorCamera* Editor::getEditorCamera()
{
    return m_editorCamera.get();
}

void Editor::setMode(EditorMode mode)
{
    m_mode = mode;
}

EditorMode Editor::getMode() const
{
    return m_mode;
}

void Editor::toggleMode()
{
    m_mode = (m_mode == EditorMode::EDIT) ? EditorMode::PLAY : EditorMode::EDIT;
}

bool Editor::wantCaptureMouse() const
{
    if (!m_isInitialized)
    {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse;
}

bool Editor::wantCaptureKeyboard() const
{
    if (!m_isInitialized)
    {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool Editor::isInitialized() const
{
    return m_isInitialized;
}

void Editor::setResourceManager(ResourceManager* resourceManager)
{
    m_resourceManager = resourceManager;

    // Initialize asset browser now that we have a resource manager
    if (resourceManager && !m_assetPath.empty())
    {
        m_assetBrowserPanel.initialize(m_assetPath, *resourceManager);
    }

    // Give the file menu access to the resource manager for scene loading
    m_fileMenu.setResourceManager(resourceManager);

    // Give the inspector access for material library operations
    m_inspectorPanel.setResourceManager(resourceManager);
}

FileMenu& Editor::getFileMenu()
{
    return m_fileMenu;
}

const FileMenu& Editor::getFileMenu() const
{
    return m_fileMenu;
}

CommandHistory& Editor::getCommandHistory()
{
    return m_commandHistory;
}

const CommandHistory& Editor::getCommandHistory() const
{
    return m_commandHistory;
}

BrushTool& Editor::getBrushTool()
{
    return m_brushTool;
}

BrushPreviewRenderer& Editor::getBrushPreview()
{
    return m_brushPreview;
}

EnvironmentPanel& Editor::getEnvironmentPanel()
{
    return m_environmentPanel;
}

void Editor::setFoliageManager(FoliageManager* manager)
{
    m_foliageManager = manager;
    m_fileMenu.setFoliageManager(manager);
}

void Editor::setTerrain(Terrain* terrain)
{
    m_terrain = terrain;
    m_fileMenu.setTerrain(terrain);
}

TerrainBrush& Editor::getTerrainBrush()
{
    return m_terrainBrush;
}

TerrainPanel& Editor::getTerrainPanel()
{
    return m_terrainPanel;
}

void Editor::setProfiler(PerformanceProfiler* profiler)
{
    m_profiler = profiler;
}

void Editor::setNavigationSystem(NavigationSystem* navSystem)
{
    m_navigationSystem = navSystem;
}

void Editor::setAudioSystem(AudioSystem* audioSystem)
{
    m_audioSystem = audioSystem;
}

void Editor::setUISystem(UISystem* uiSystem)
{
    m_uiSystem = uiSystem;
}

PerformancePanel& Editor::getPerformancePanel()
{
    return m_performancePanel;
}

void Editor::showNotification(const std::string& text)
{
    m_notifyText = text;
    m_notifyTimer = 2.0f;
}

void Editor::processViewportClick(int fboWidth, int fboHeight)
{
    m_pickRequested = false;

    if (!m_isInitialized || m_mode != EditorMode::EDIT)
    {
        m_boxSelectActive = false;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    float vpWidth = m_viewportMax.x - m_viewportMin.x;
    float vpHeight = m_viewportMax.y - m_viewportMin.y;
    if (vpWidth <= 0.0f || vpHeight <= 0.0f)
    {
        return;
    }

    // Helper: map screen pos to FBO pixel coords
    auto screenToFbo = [&](float sx, float sy, int& outX, int& outY)
    {
        float u = (sx - m_viewportMin.x) / vpWidth;
        float v = (sy - m_viewportMin.y) / vpHeight;
        outX = std::clamp(static_cast<int>(u * static_cast<float>(fboWidth)), 0, fboWidth - 1);
        outY = std::clamp(static_cast<int>((1.0f - v) * static_cast<float>(fboHeight)), 0, fboHeight - 1);
    };

    // --- Start a drag on LMB press in viewport ---
    if (io.MouseClicked[0] && m_viewportHovered && !io.KeyAlt && !m_gizmoActive)
    {
        m_boxSelectActive = true;
        m_boxSelectStart = glm::vec2(io.MousePos.x, io.MousePos.y);
        m_boxSelectEnd = m_boxSelectStart;
    }

    // --- Update drag end position while dragging ---
    if (m_boxSelectActive && io.MouseDown[0])
    {
        m_boxSelectEnd = glm::vec2(io.MousePos.x, io.MousePos.y);
    }

    // --- On LMB release, decide: point pick or box select ---
    if (m_boxSelectActive && !io.MouseDown[0])
    {
        m_boxSelectActive = false;
        float dx = std::abs(m_boxSelectEnd.x - m_boxSelectStart.x);
        float dy = std::abs(m_boxSelectEnd.y - m_boxSelectStart.y);

        m_pickShift = io.KeyShift;
        m_pickCtrl = io.KeyCtrl;

        if (dx < 5.0f && dy < 5.0f)
        {
            // Small drag = point pick (same as before)
            screenToFbo(m_boxSelectStart.x, m_boxSelectStart.y, m_pickX, m_pickY);
            m_pickRequested = true;
        }
        else
        {
            // Large drag = box select
            screenToFbo(m_boxSelectStart.x, m_boxSelectStart.y, m_boxPickX0, m_boxPickY0);
            screenToFbo(m_boxSelectEnd.x, m_boxSelectEnd.y, m_boxPickX1, m_boxPickY1);
            m_boxSelectPending = true;
        }
    }
}

bool Editor::isPickRequested() const
{
    return m_pickRequested;
}

void Editor::getPickCoords(int& outX, int& outY) const
{
    outX = m_pickX;
    outY = m_pickY;
}

void Editor::handlePickResult(uint32_t entityId)
{
    m_pickRequested = false;

    if (m_pickShift)
    {
        // Shift+click: add to selection
        m_selection.addToSelection(entityId);
    }
    else if (m_pickCtrl)
    {
        // Ctrl+click: toggle in selection
        m_selection.toggleSelection(entityId);
    }
    else
    {
        // Plain click: replace selection (or clear if background)
        m_selection.select(entityId);
    }
}

Selection& Editor::getSelection()
{
    return m_selection;
}

const Selection& Editor::getSelection() const
{
    return m_selection;
}

void Editor::getViewportSize(int& outWidth, int& outHeight) const
{
    outWidth = static_cast<int>(m_viewportMax.x - m_viewportMin.x);
    outHeight = static_cast<int>(m_viewportMax.y - m_viewportMin.y);
}

bool Editor::isGizmoActive() const
{
    return m_gizmoActive;
}

void Editor::drawGizmo(Camera* camera, Scene* scene)
{
    // Reset gizmo active state (updated below if gizmo is drawn)
    m_gizmoActive = false;

    if (!camera || !scene || !m_selection.hasSelection())
    {
        return;
    }

    float vpWidth = m_viewportMax.x - m_viewportMin.x;
    float vpHeight = m_viewportMax.y - m_viewportMin.y;
    if (vpWidth <= 0.0f || vpHeight <= 0.0f)
    {
        return;
    }

    Entity* entity = m_selection.getPrimaryEntity(*scene);
    if (!entity)
    {
        return;
    }

    // Configure ImGuizmo for perspective rendering in this viewport
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(m_viewportMin.x, m_viewportMin.y, vpWidth, vpHeight);

    // Get camera matrices — use standard projection (not reverse-Z) for ImGuizmo
    glm::mat4 view = camera->getViewMatrix();
    float vpAspect = vpWidth / vpHeight;
    glm::mat4 proj = camera->getCullingProjectionMatrix(vpAspect);

    // Get the entity's world transform as the manipulated matrix
    glm::mat4 model = entity->getWorldMatrix();

    // Set up snap values (active when Ctrl is held)
    float snap[3] = {0.0f, 0.0f, 0.0f};
    bool useSnap = ImGui::GetIO().KeyCtrl;
    if (useSnap)
    {
        float snapVal = m_snapTranslation;
        if (m_gizmoOperation == ImGuizmo::ROTATE)
        {
            snapVal = m_snapRotation;
        }
        else if (m_gizmoOperation == ImGuizmo::SCALE)
        {
            snapVal = m_snapScale;
        }
        snap[0] = snap[1] = snap[2] = snapVal;
    }

    // Draw and manipulate the gizmo
    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        m_gizmoOperation,
        m_gizmoMode,
        glm::value_ptr(model),
        nullptr,
        useSnap ? snap : nullptr
    );

    // Apply the manipulated transform back to the entity
    if (ImGuizmo::IsUsing())
    {
        // Convert world-space result to local space (account for parent transform)
        glm::mat4 localMatrix = model;
        const Entity* parent = entity->getParent();
        if (parent)
        {
            localMatrix = glm::inverse(parent->getWorldMatrix()) * model;
        }

        // Decompose into translation, rotation (degrees), scale
        float translation[3];
        float rotation[3];
        float scale[3];
        ImGuizmo::DecomposeMatrixToComponents(
            glm::value_ptr(localMatrix),
            translation, rotation, scale
        );

        entity->transform.position = glm::vec3(translation[0], translation[1], translation[2]);
        entity->transform.rotation = glm::vec3(rotation[0], rotation[1], rotation[2]);
        entity->transform.scale = glm::vec3(scale[0], scale[1], scale[2]);

        // Clear matrix override if present (user is now editing via TRS gizmo)
        if (entity->transform.hasMatrixOverride())
        {
            entity->transform.clearMatrixOverride();
        }
    }

    // Track gizmo hover/use for next frame's pick suppression
    bool isUsing = ImGuizmo::IsUsing();
    m_gizmoActive = ImGuizmo::IsOver() || isUsing;

    // Gizmo begin/end bracketing for undo — capture old transform on drag start,
    // record TransformCommand on drag end. One drag = one undo step.
    if (!m_wasGizmoUsing && isUsing)
    {
        // Drag begins — snapshot the current transform
        m_gizmoStartEntityId = entity->getId();
        m_gizmoStartPosition = entity->transform.position;
        m_gizmoStartRotation = entity->transform.rotation;
        m_gizmoStartScale = entity->transform.scale;
    }
    else if (m_wasGizmoUsing && !isUsing)
    {
        // Drag ends — record a TransformCommand if the transform actually changed
        if (scene && m_gizmoStartEntityId != 0)
        {
            const Entity* target = scene->findEntityById(m_gizmoStartEntityId);
            if (target)
            {
                bool changed = (target->transform.position != m_gizmoStartPosition)
                             || (target->transform.rotation != m_gizmoStartRotation)
                             || (target->transform.scale != m_gizmoStartScale);
                if (changed)
                {
                    auto cmd = std::make_unique<TransformCommand>(
                        *scene, m_gizmoStartEntityId,
                        m_gizmoStartPosition, m_gizmoStartRotation, m_gizmoStartScale,
                        target->transform.position, target->transform.rotation, target->transform.scale);
                    m_commandHistory.execute(std::move(cmd));
                }
            }
        }
        m_gizmoStartEntityId = 0;
    }
    m_wasGizmoUsing = isUsing;
}

void Editor::drawGizmoOverlay()
{
    // Show gizmo mode indicator in the top-left corner of the viewport
    const char* opName = "Translate (W)";
    if (m_gizmoOperation == ImGuizmo::ROTATE)
    {
        opName = "Rotate (E)";
    }
    else if (m_gizmoOperation == ImGuizmo::SCALE)
    {
        opName = "Scale (R)";
    }

    const char* modeName = (m_gizmoMode == ImGuizmo::WORLD) ? "World" : "Local";

    char overlayText[128];
    std::snprintf(overlayText, sizeof(overlayText),
        "%s | %s (L) | Ctrl=Snap", opName, modeName);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 textPos(m_viewportMin.x + 10.0f, m_viewportMin.y + 8.0f);
    ImVec2 textSize = ImGui::CalcTextSize(overlayText);

    // Semi-transparent background pill
    drawList->AddRectFilled(
        ImVec2(textPos.x - 6.0f, textPos.y - 4.0f),
        ImVec2(textPos.x + textSize.x + 6.0f, textPos.y + textSize.y + 4.0f),
        IM_COL32(25, 25, 30, 192),
        4.0f
    );

    // Warm yellow text for visibility
    drawList->AddText(textPos, IM_COL32(255, 230, 150, 216), overlayText);
}

void Editor::processGizmoShortcuts()
{
    // Only process when viewport is focused and no text input is active
    if (!m_viewportFocused || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_W))
    {
        m_gizmoOperation = ImGuizmo::TRANSLATE;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E))
    {
        m_gizmoOperation = ImGuizmo::ROTATE;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R))
    {
        m_gizmoOperation = ImGuizmo::SCALE;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_L))
    {
        m_gizmoMode = (m_gizmoMode == ImGuizmo::WORLD)
            ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_G))
    {
        m_showGrid = !m_showGrid;
    }
    const ImGuiIO& gizmoIo = ImGui::GetIO();
    if (gizmoIo.KeyCtrl && gizmoIo.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F))
    {
        m_fullscreenViewport = !m_fullscreenViewport;
    }
}

void Editor::processEntityShortcuts(Scene* scene)
{
    // Only process when viewport is focused, no text input active, and in edit mode
    if (!m_viewportFocused || !scene || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();

    // Delete key — delete all selected entities (via commands for undo)
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_selection.hasSelection())
    {
        std::vector<uint32_t> ids = m_selection.getSelectedIds();
        m_selection.clearSelection();

        if (ids.size() == 1)
        {
            auto cmd = std::make_unique<DeleteEntityCommand>(*scene, ids[0]);
            m_commandHistory.execute(std::move(cmd));
        }
        else if (ids.size() > 1)
        {
            // Multi-delete: wrap in CompositeCommand
            std::vector<std::unique_ptr<EditorCommand>> cmds;
            for (uint32_t id : ids)
            {
                cmds.push_back(std::make_unique<DeleteEntityCommand>(*scene, id));
            }
            m_commandHistory.execute(
                std::make_unique<CompositeCommand>(
                    "Delete " + std::to_string(ids.size()) + " entities",
                    std::move(cmds)));
        }
    }

    // Ctrl+D — duplicate primary selected entity (via command for undo)
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D)
        && m_selection.hasSelection())
    {
        const Entity* clone = EntityActions::duplicateEntity(*scene, m_selection, m_selection.getPrimaryId());
        if (clone)
        {
            m_commandHistory.execute(
                std::make_unique<CreateEntityCommand>(*scene, clone->getId()));
        }
    }

    // Ctrl+G — group selected entities
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G)
        && m_selection.hasSelection())
    {
        EntityActions::groupEntities(*scene, m_selection);
        // Group is complex (creates entity + reparents) — mark dirty directly
        // until we have a dedicated GroupCommand
        m_fileMenu.markDirty();
    }

    // H — toggle visibility of primary selected entity
    if (!io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_H)
        && m_selection.hasSelection())
    {
        const Entity* entity = scene->findEntityById(m_selection.getPrimaryId());
        if (entity)
        {
            bool oldVis = entity->isVisible();
            m_commandHistory.execute(
                std::make_unique<EntityPropertyCommand>(
                    *scene, entity->getId(),
                    EntityProperty::VISIBLE, oldVis, !oldVis));
        }
    }

    // Ctrl+I — open import model dialog
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_I))
    {
        m_importDialog.open();
    }

    // Ctrl+Shift+C — copy transform
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C)
        && m_selection.hasSelection())
    {
        EntityActions::copyTransform(*scene, m_selection.getPrimaryId(), m_transformClipboard);
    }

    // Ctrl+Shift+V — paste transform
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V)
        && m_selection.hasSelection())
    {
        EntityActions::pasteTransform(*scene, m_selection.getPrimaryId(), m_transformClipboard);
    }
}

/*static*/ void Editor::setupTheme()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    // Rounded corners for a modern look
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;

    // Generous padding for accessibility (larger click targets)
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    style.ScrollbarSize = 16.0f;
    style.GrabMinSize = 14.0f;

    // High-contrast dark theme
    ImVec4* colors = style.Colors;

    // Window backgrounds — dark
    colors[ImGuiCol_WindowBg]           = ImVec4(0.10f, 0.10f, 0.12f, 0.95f);
    colors[ImGuiCol_ChildBg]            = ImVec4(0.10f, 0.10f, 0.12f, 0.00f);
    colors[ImGuiCol_PopupBg]            = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);

    // Borders — visible but not harsh
    colors[ImGuiCol_Border]             = ImVec4(0.30f, 0.30f, 0.35f, 0.65f);
    colors[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame backgrounds (input fields, sliders)
    colors[ImGuiCol_FrameBg]            = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.28f, 0.28f, 0.36f, 1.00f);

    // Title bar
    colors[ImGuiCol_TitleBg]            = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.08f, 0.08f, 0.10f, 0.75f);

    // Menu bar
    colors[ImGuiCol_MenuBarBg]          = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.08f, 0.08f, 0.10f, 0.80f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.48f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.50f, 0.60f, 1.00f);

    // Accent color — warm orange for buttons and interactive elements
    colors[ImGuiCol_Button]             = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.85f, 0.55f, 0.15f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.95f, 0.60f, 0.10f, 1.00f);

    // Headers (collapsible sections, tree nodes)
    colors[ImGuiCol_Header]             = ImVec4(0.20f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.85f, 0.55f, 0.15f, 0.80f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.95f, 0.60f, 0.10f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab]               = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TabHovered]        = ImVec4(0.85f, 0.55f, 0.15f, 0.80f);
    colors[ImGuiCol_TabSelected]       = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_TabDimmed]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);

    // Docking
    colors[ImGuiCol_DockingPreview]     = ImVec4(0.85f, 0.55f, 0.15f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]     = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

    // Separator
    colors[ImGuiCol_Separator]          = ImVec4(0.28f, 0.28f, 0.35f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]   = ImVec4(0.85f, 0.55f, 0.15f, 0.78f);
    colors[ImGuiCol_SeparatorActive]    = ImVec4(0.95f, 0.60f, 0.10f, 1.00f);

    // Resize grip
    colors[ImGuiCol_ResizeGrip]         = ImVec4(0.28f, 0.28f, 0.35f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]  = ImVec4(0.85f, 0.55f, 0.15f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]   = ImVec4(0.95f, 0.60f, 0.10f, 0.95f);

    // Check mark and slider grab
    colors[ImGuiCol_CheckMark]          = ImVec4(0.95f, 0.60f, 0.10f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.70f, 0.45f, 0.12f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.95f, 0.60f, 0.10f, 1.00f);

    // Text — bright white for maximum contrast
    colors[ImGuiCol_Text]              = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]      = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
}

} // namespace Vestige
