/// @file editor.cpp
/// @brief Editor implementation — ImGui lifecycle, docking workspace, editor camera, theme.
#include "editor/editor.h"
#include "core/logger.h"
#include "renderer/renderer.h"
#include "scene/scene.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <algorithm>

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
}

void Editor::drawPanels(Renderer* renderer, Scene* scene)
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

            ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.20f, &left, &main);
            ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.25f, &right, &main);
            ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.25f, &bottom, &main);

            ImGui::DockBuilderDockWindow("Hierarchy", left);
            ImGui::DockBuilderDockWindow("Inspector", right);
            ImGui::DockBuilderDockWindow("Console", bottom);
            ImGui::DockBuilderDockWindow("Viewport", main);

            ImGui::DockBuilderFinish(dockspaceId);
        }

        ImGui::DockSpaceOverViewport(dockspaceId, viewport);

        // Menu bar
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene", "Ctrl+N"))
                {
                    // TODO: Phase 5D
                }
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
                {
                    // TODO: Phase 5D
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                {
                    // TODO: Phase 5D
                }
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                {
                    // TODO: Phase 5D
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Q"))
                {
                    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false))
                {
                    // TODO: Phase 5D
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false))
                {
                    // TODO: Phase 5D
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Demo Window", nullptr, &m_showDemoWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Create"))
            {
                if (ImGui::MenuItem("Empty Entity"))
                {
                    // TODO: Phase 5B
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Primitives"))
                {
                    ImGui::MenuItem("Cube", nullptr, false, false);
                    ImGui::MenuItem("Plane", nullptr, false, false);
                    ImGui::MenuItem("Sphere", nullptr, false, false);
                    ImGui::MenuItem("Cylinder", nullptr, false, false);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Lights"))
                {
                    ImGui::MenuItem("Directional Light", nullptr, false, false);
                    ImGui::MenuItem("Point Light", nullptr, false, false);
                    ImGui::MenuItem("Spot Light", nullptr, false, false);
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

            // Store viewport bounds for click detection (used next frame in processViewportClick)
            m_viewportMin = glm::vec2(contentMin.x, contentMin.y);
            m_viewportMax = glm::vec2(contentMin.x + viewportSize.x, contentMin.y + viewportSize.y);

            m_viewportFocused = ImGui::IsWindowFocused();
            m_viewportHovered = ImGui::IsWindowHovered();
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // --- Hierarchy panel ---
        ImGui::Begin("Hierarchy");
        m_hierarchyPanel.draw(scene, m_selection);
        ImGui::End();

        // --- Inspector panel ---
        ImGui::Begin("Inspector");
        m_inspectorPanel.draw(scene, m_selection);
        ImGui::End();

        ImGui::Begin("Console");
        ImGui::TextWrapped("Log output will appear here.");
        ImGui::TextWrapped("(Phase 5F)");
        ImGui::End();

        // Demo window for testing ImGui features
        if (m_showDemoWindow)
        {
            ImGui::ShowDemoWindow(&m_showDemoWindow);
        }
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
