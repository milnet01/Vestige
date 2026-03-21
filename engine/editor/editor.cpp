/// @file editor.cpp
/// @brief Editor implementation — ImGui lifecycle, docking workspace, editor camera, theme.
#include "editor/editor.h"
#include "editor/commands/transform_command.h"
#include "editor/commands/create_entity_command.h"
#include "editor/commands/delete_entity_command.h"
#include "editor/commands/composite_command.h"
#include "editor/commands/entity_property_command.h"
#include "core/logger.h"
#include "renderer/camera.h"
#include "renderer/renderer.h"
#include "resource/resource_manager.h"
#include "scene/entity.h"
#include "scene/scene.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
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

    // Load editor font (larger for accessibility — user is partially sighted)
    std::string fontPath = assetPath + "/fonts/default.ttf";
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

    // Note: asset browser is initialized when setResourceManager() is called
    // because it needs a ResourceManager for texture loading.

    // Initialize file menu with the GLFW window for title bar updates
    m_fileMenu.setWindow(window);
    m_fileMenu.setCommandHistory(&m_commandHistory);

    // Wire command history to panels for undo support
    m_inspectorPanel.setCommandHistory(&m_commandHistory);
    m_hierarchyPanel.setCommandHistory(&m_commandHistory);

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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_isInitialized = false;
    m_window = nullptr;
    Logger::info("Editor shut down");
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

void Editor::drawPanels(Renderer* renderer, Scene* scene, Camera* camera)
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
            ImGui::DockBuilderDockWindow("Viewport", main);

            ImGui::DockBuilderFinish(dockspaceId);
        }

        ImGui::DockSpaceOverViewport(dockspaceId, viewport);

        // Process global file shortcuts (Ctrl+N/O/S/Shift+S/Q)
        m_fileMenu.processShortcuts(scene, m_selection);

        // Process undo/redo shortcuts (Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z)
        {
            ImGuiIO& undoIo = ImGui::GetIO();
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
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                bool envOpen = m_environmentPanel.isOpen();
                if (ImGui::MenuItem("Environment", nullptr, &envOpen))
                {
                    m_environmentPanel.setOpen(envOpen);
                }
                ImGui::MenuItem("Demo Window", nullptr, &m_showDemoWindow);
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
                if (ImGui::MenuItem("Controls"))
                {
                    // TODO: show controls overlay
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

            m_viewportFocused = ImGui::IsWindowFocused();
            m_viewportHovered = ImGui::IsWindowHovered();
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // --- Hierarchy panel ---
        ImGui::Begin("Hierarchy");
        m_hierarchyPanel.draw(scene, m_selection);
        ImGui::End();

        // Process pending "Save as Prefab" from hierarchy context menu
        if (m_hierarchyPanel.hasPendingSavePrefab() && scene && m_resourceManager)
        {
            Entity* entity = scene->findEntityById(
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

        ImGui::Begin("Console");
        ImGui::TextWrapped("Log output will appear here.");
        ImGui::TextWrapped("(Phase 5F)");
        ImGui::End();

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
            m_environmentPanel.draw(m_brushTool, *m_foliageManager, m_commandHistory);
        }

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

        // Update window title to reflect current dirty state
        if (scene)
        {
            m_fileMenu.updateWindowTitle(scene->getName());
        }

        // Tick auto-save timer (writes autosave file every 120s when dirty)
        m_fileMenu.tickAutoSave(scene);
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
}

void Editor::processViewportClick(int fboWidth, int fboHeight)
{
    m_pickRequested = false;

    if (!m_isInitialized || m_mode != EditorMode::EDIT)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Check for left mouse click while the viewport is hovered (previous frame's state)
    if (!io.MouseClicked[0] || !m_viewportHovered)
    {
        return;
    }

    // Ignore clicks while Alt is held (orbit camera uses Alt+LMB)
    if (io.KeyAlt)
    {
        return;
    }

    // Don't pick when gizmo was hovered/used (previous frame state)
    if (m_gizmoActive)
    {
        return;
    }

    float mx = io.MousePos.x;
    float my = io.MousePos.y;

    // Check if click is within stored viewport bounds
    float vpWidth = m_viewportMax.x - m_viewportMin.x;
    float vpHeight = m_viewportMax.y - m_viewportMin.y;
    if (vpWidth <= 0.0f || vpHeight <= 0.0f)
    {
        return;
    }
    if (mx < m_viewportMin.x || mx > m_viewportMax.x
        || my < m_viewportMin.y || my > m_viewportMax.y)
    {
        return;
    }

    // Map to viewport UV [0,1]
    float u = (mx - m_viewportMin.x) / vpWidth;
    float v = (my - m_viewportMin.y) / vpHeight;

    // Map to FBO pixel coordinates (flip Y for OpenGL)
    m_pickX = static_cast<int>(u * static_cast<float>(fboWidth));
    m_pickY = static_cast<int>((1.0f - v) * static_cast<float>(fboHeight));
    m_pickX = std::clamp(m_pickX, 0, fboWidth - 1);
    m_pickY = std::clamp(m_pickY, 0, fboHeight - 1);

    m_pickShift = io.KeyShift;
    m_pickCtrl = io.KeyCtrl;
    m_pickRequested = true;
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
        Entity* parent = entity->getParent();
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
            Entity* target = scene->findEntityById(m_gizmoStartEntityId);
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
}

void Editor::processEntityShortcuts(Scene* scene)
{
    // Only process when viewport is focused, no text input active, and in edit mode
    if (!m_viewportFocused || !scene || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

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
        Entity* clone = EntityActions::duplicateEntity(*scene, m_selection, m_selection.getPrimaryId());
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
        Entity* entity = scene->findEntityById(m_selection.getPrimaryId());
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

void Editor::setupTheme()
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
