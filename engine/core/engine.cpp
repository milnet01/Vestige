/// @file engine.cpp
/// @brief Engine implementation — main loop and subsystem orchestration.
#include "core/engine.h"
#include "core/logger.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "scene/water_surface.h"
#include "resource/model.h"
#include "renderer/frame_diagnostics.h"
#include "renderer/debug_draw.h"
#include "renderer/light_utils.h"
#include "editor/tools/brush_tool.h"
#include "profiler/cpu_profiler.h"

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Vestige
{

Engine::Engine()
    : m_isRunning(false)
    , m_isCursorCaptured(true)
{
}

Engine::~Engine()
{
    shutdown();
}

bool Engine::initialize(const EngineConfig& config)
{
    Logger::info("=== Vestige Engine v0.5.0 ===");
    Logger::info("Initializing engine...");

    // Create window (also initializes GLFW and OpenGL context)
    m_window = std::make_unique<Window>(config.window, m_eventBus);

    // Create timer
    m_timer = std::make_unique<Timer>();

    // Create input manager
    m_inputManager = std::make_unique<InputManager>(m_window->getHandle(), m_eventBus);

    // Create renderer
    m_renderer = std::make_unique<Renderer>(m_eventBus);

    // Load shaders
    if (!m_renderer->loadShaders(config.assetPath))
    {
        Logger::fatal("Failed to load shaders — cannot continue");
        return false;
    }

    // Initialize framebuffer pipeline (4x MSAA)
    m_renderer->initFramebuffers(m_window->getWidth(), m_window->getHeight(), 4);

    // Initialize text rendering (optional — continues without it)
    if (!m_renderer->initTextRenderer(config.assetPath + "/fonts/default.ttf", config.assetPath))
    {
        Logger::warning("Text rendering unavailable (font not found)");
    }

    // Create resource manager
    m_resourceManager = std::make_unique<ResourceManager>();

    // Create scene manager
    m_sceneManager = std::make_unique<SceneManager>();

    // Create camera — start slightly above the ground
    m_camera = std::make_unique<Camera>(glm::vec3(0.0f, 1.7f, 5.0f));

    // Create first-person controller
    ControllerConfig ctrlConfig;
    ctrlConfig.playerHeight = 1.7f;
    ctrlConfig.moveSpeed = 3.0f;
    m_controller = std::make_unique<FirstPersonController>(*m_camera, *m_inputManager, ctrlConfig);

    // Set up the demo scene
    setupDemoScene();

    // Initialize editor (ImGui + docking) — must come after InputManager for callback chaining
    m_editor = std::make_unique<Editor>();
    if (!m_editor->initialize(m_window->getHandle(), config.assetPath))
    {
        Logger::warning("Editor initialization failed — continuing without editor");
        m_editor.reset();
    }

    // Initialize debug draw system for editor overlays (light gizmos, etc.)
    if (!m_debugDraw.initialize(config.assetPath))
    {
        Logger::warning("DebugDraw initialization failed — gizmos will be unavailable");
    }

    // Initialize particle renderer
    if (!m_particleRenderer.init(config.assetPath))
    {
        Logger::warning("Particle renderer initialization failed — particles will be unavailable");
    }

    // Initialize water renderer and FBOs
    if (!m_waterRenderer.init(config.assetPath))
    {
        Logger::warning("Water renderer initialization failed — water surfaces will be unavailable");
    }
    {
        int w = config.window.width;
        int h = config.window.height;
        m_waterFbo.init(w / 2, h / 2, w / 2, h / 2);
    }

    // Initialize performance profiler
    m_profiler.init();

    // Initialize foliage renderer
    if (!m_foliageRenderer.init(config.assetPath))
    {
        Logger::warning("Foliage renderer initialization failed — foliage will be unavailable");
    }

    // Initialize tree renderer
    if (!m_treeRenderer.init(config.assetPath))
    {
        Logger::warning("Tree renderer initialization failed — trees will be unavailable");
    }

    // Initialize terrain renderer
    if (!m_terrainRenderer.init(config.assetPath))
    {
        Logger::warning("Terrain renderer initialization failed — terrain will be unavailable");
    }

    // Initialize terrain with default config
    {
        TerrainConfig terrainConfig;
        terrainConfig.width = 257;
        terrainConfig.depth = 257;
        terrainConfig.spacingX = 1.0f;
        terrainConfig.spacingZ = 1.0f;
        terrainConfig.heightScale = 50.0f;
        terrainConfig.origin = glm::vec3(-128.0f, 0.0f, -128.0f);  // Center at origin
        terrainConfig.gridResolution = 33;
        terrainConfig.maxLodLevels = 6;
        terrainConfig.baseLodDistance = 20.0f;

        if (m_terrain.initialize(terrainConfig))
        {
            // Generate test hills with a flat area around the demo objects
            int w = terrainConfig.width;
            int d = terrainConfig.depth;
            float originX = terrainConfig.origin.x;
            float originZ = terrainConfig.origin.z;
            for (int z = 0; z < d; ++z)
            {
                for (int x = 0; x < w; ++x)
                {
                    float nx = static_cast<float>(x) / static_cast<float>(w - 1);
                    float nz = static_cast<float>(z) / static_cast<float>(d - 1);

                    // World position of this texel
                    float wx = originX + static_cast<float>(x) * terrainConfig.spacingX;
                    float wz = originZ + static_cast<float>(z) * terrainConfig.spacingZ;

                    // Gentle rolling hills using sine waves
                    float h = 0.0f;
                    h += 0.15f * std::sin(nx * 6.28f * 2.0f) * std::cos(nz * 6.28f * 1.5f);
                    h += 0.08f * std::sin(nx * 6.28f * 5.0f + 1.0f) * std::sin(nz * 6.28f * 4.0f);
                    h += 0.04f * std::sin(nx * 6.28f * 11.0f) * std::cos(nz * 6.28f * 9.0f + 2.0f);
                    h = std::max(h, 0.0f);

                    // Flatten near origin where demo objects and grass sit.
                    // Ground plane sits at y=0.02, so flat terrain at y=0 is just below it.
                    float distFromOrigin = std::sqrt(wx * wx + wz * wz);
                    float flatRadius = 18.0f;   // Fully flat within 18m (ground plane is 30m)
                    float blendRadius = 35.0f;  // Blend from flat to hills
                    if (distFromOrigin < blendRadius)
                    {
                        float t = std::max(0.0f, (distFromOrigin - flatRadius)
                                                / (blendRadius - flatRadius));
                        h *= t * t;  // Quadratic ease-in for smooth transition
                    }

                    m_terrain.setRawHeight(x, z, h);
                }
            }

            // Upload modified heightmap and recompute normals
            m_terrain.updateHeightmapRegion(0, 0, w, d);
            m_terrain.updateNormalMapRegion(0, 0, w, d);
            m_terrain.buildQuadtree();  // Rebuild with correct min/max heights
        }
        else
        {
            Logger::warning("Terrain initialization failed");
        }
    }

    // Wire up foliage shadow casting into the renderer's shadow pass
    m_renderer->setFoliageShadowCaster(&m_foliageRenderer, &m_foliageManager);

    // Give the editor access to the resource manager and foliage manager
    if (m_editor)
    {
        m_editor->setResourceManager(m_resourceManager.get());
        m_editor->setFoliageManager(&m_foliageManager);
        m_editor->setTerrain(&m_terrain);
        m_editor->setProfiler(&m_profiler);
        m_editor->getBrushPreview().init(config.assetPath);
    }

    // Start in editor mode — cursor visible, FPS controller disabled
    if (m_editor)
    {
        m_window->setCursorEnabled(true);
        m_isCursorCaptured = false;
        m_controller->setEnabled(false);
    }
    else
    {
        // Fallback: no editor, start in play mode
        m_window->setCursorEnabled(false);
        m_isCursorCaptured = true;
    }

    // Subscribe to window close event
    m_eventBus.subscribe<WindowCloseEvent>([this](const WindowCloseEvent&)
    {
        if (m_editor)
        {
            m_editor->getFileMenu().requestQuit();
        }
        else
        {
            m_isRunning = false;
        }
    });

    // Subscribe to key events for engine controls
    m_eventBus.subscribe<KeyPressedEvent>([this](const KeyPressedEvent& event)
    {
        if (event.isRepeat)
        {
            return;
        }

        // When ImGui wants keyboard input, only let Escape, F-keys, and
        // editor camera keys (numpad presets, F for focus) through
        if (m_editor && m_editor->wantCaptureKeyboard()
            && event.keyCode != GLFW_KEY_ESCAPE
            && !(event.keyCode >= GLFW_KEY_F1 && event.keyCode <= GLFW_KEY_F12)
            && event.keyCode != GLFW_KEY_KP_1
            && event.keyCode != GLFW_KEY_KP_3
            && event.keyCode != GLFW_KEY_KP_7
            && event.keyCode != GLFW_KEY_F)
        {
            return;
        }

        switch (event.keyCode)
        {
            case GLFW_KEY_ESCAPE:
                if (m_editor)
                {
                    // Toggle between editor and play mode
                    m_editor->toggleMode();
                    bool isPlayMode = (m_editor->getMode() == EditorMode::PLAY);
                    m_isCursorCaptured = isPlayMode;
                    m_window->setCursorEnabled(!isPlayMode);
                    m_controller->setEnabled(isPlayMode);

                    // When returning to edit mode, sync the editor camera to
                    // where the FPS camera is so it doesn't snap back
                    if (!isPlayMode && m_editor->getEditorCamera() && m_camera)
                    {
                        m_editor->getEditorCamera()->syncFromCamera(*m_camera);
                    }

                    Logger::info(isPlayMode ? "Switched to PLAY mode" : "Switched to EDIT mode");
                }
                else
                {
                    // No editor: toggle cursor capture as before
                    m_isCursorCaptured = !m_isCursorCaptured;
                    m_window->setCursorEnabled(!m_isCursorCaptured);
                    m_controller->setEnabled(m_isCursorCaptured);
                }
                break;

            case GLFW_KEY_F1:
                m_renderer->setWireframeMode(!m_renderer->isWireframeMode());
                break;

            case GLFW_KEY_F2:
            {
                int nextMode = (m_renderer->getTonemapMode() + 1) % 3;
                m_renderer->setTonemapMode(nextMode);
                const char* names[] = {"Reinhard", "ACES Filmic", "None (linear clamp)"};
                Logger::info("Tonemapper: " + std::string(names[nextMode]));
                break;
            }

            case GLFW_KEY_F3:
            {
                int nextDebug = (m_renderer->getDebugMode() == 0) ? 1 : 0;
                m_renderer->setDebugMode(nextDebug);
                Logger::info(std::string("HDR debug: ") + (nextDebug ? "false-color luminance" : "off"));
                break;
            }

            case GLFW_KEY_LEFT_BRACKET:
            {
                m_renderer->setAutoExposure(false);  // Manual override disables auto
                float newExposure = m_renderer->getExposure() - 0.1f;
                if (newExposure < 0.1f)
                {
                    newExposure = 0.1f;
                }
                m_renderer->setExposure(newExposure);
                Logger::info("Exposure: " + std::to_string(newExposure) + " (manual)");
                break;
            }

            case GLFW_KEY_RIGHT_BRACKET:
            {
                m_renderer->setAutoExposure(false);  // Manual override disables auto
                float newExposure = m_renderer->getExposure() + 0.1f;
                if (newExposure > 10.0f)
                {
                    newExposure = 10.0f;
                }
                m_renderer->setExposure(newExposure);
                Logger::info("Exposure: " + std::to_string(newExposure) + " (manual)");
                break;
            }

            case GLFW_KEY_F4:
                m_renderer->setPomEnabled(!m_renderer->isPomEnabled());
                Logger::info(std::string("POM: ") + (m_renderer->isPomEnabled() ? "ON" : "OFF"));
                break;

            case GLFW_KEY_MINUS:
            {
                float newMult = m_renderer->getPomHeightMultiplier() - 0.1f;
                if (newMult < 0.0f)
                {
                    newMult = 0.0f;
                }
                m_renderer->setPomHeightMultiplier(newMult);
                Logger::info("POM height multiplier: " + std::to_string(newMult));
                break;
            }

            case GLFW_KEY_EQUAL:
            {
                float newMult = m_renderer->getPomHeightMultiplier() + 0.1f;
                if (newMult > 3.0f)
                {
                    newMult = 3.0f;
                }
                m_renderer->setPomHeightMultiplier(newMult);
                Logger::info("POM height multiplier: " + std::to_string(newMult));
                break;
            }

            case GLFW_KEY_F5:
                m_renderer->setBloomEnabled(!m_renderer->isBloomEnabled());
                Logger::info(std::string("Bloom: ") + (m_renderer->isBloomEnabled() ? "ON" : "OFF"));
                break;

            case GLFW_KEY_F6:
                m_renderer->setSsaoEnabled(!m_renderer->isSsaoEnabled());
                Logger::info(std::string("SSAO: ") + (m_renderer->isSsaoEnabled() ? "ON" : "OFF"));
                break;

            case GLFW_KEY_F7:
            {
                int current = static_cast<int>(m_renderer->getAntiAliasMode());
                int next = (current + 1) % 4;
                m_renderer->setAntiAliasMode(static_cast<AntiAliasMode>(next));
                break;
            }

            case GLFW_KEY_F8:
            {
                if (!m_renderer->isColorGradingEnabled())
                {
                    m_renderer->setColorGradingEnabled(true);
                    // Skip neutral (index 0) — start at first visual preset
                    m_renderer->nextColorGradingPreset();
                    Logger::info("Color grading: " + m_renderer->getColorGradingPresetName());
                }
                else
                {
                    m_renderer->nextColorGradingPreset();
                    // If we cycled back to Neutral (index 0), turn off
                    if (m_renderer->getColorGradingPresetName() == "Neutral")
                    {
                        m_renderer->setColorGradingEnabled(false);
                        Logger::info("Color grading: OFF");
                    }
                    else
                    {
                        Logger::info("Color grading: " + m_renderer->getColorGradingPresetName());
                    }
                }
                break;
            }

            case GLFW_KEY_F9:
            {
                bool debug = !m_renderer->isCascadeDebug();
                m_renderer->setCascadeDebug(debug);
                Logger::info(std::string("CSM debug: ") + (debug ? "ON" : "OFF"));
                break;
            }

            case GLFW_KEY_F10:
                m_renderer->setAutoExposure(!m_renderer->isAutoExposure());
                Logger::info(std::string("Auto-exposure: ") + (m_renderer->isAutoExposure() ? "ON" : "OFF"));
                break;

            case GLFW_KEY_F11:
                FrameDiagnostics::capture(*m_renderer, *m_camera,
                    m_window->getWidth(), m_window->getHeight(),
                    m_timer->getFps(), m_timer->getDeltaTime());
                break;

            // Q key: quit is now Ctrl+Q, handled by FileMenu via ImGui shortcuts.
            // Plain Q is no longer a quit shortcut.

            // --- Editor camera view presets (only active in EDIT mode) ---
            case GLFW_KEY_KP_1:
                if (m_editor && m_editor->getMode() == EditorMode::EDIT)
                {
                    m_editor->getEditorCamera()->setFrontView();
                    Logger::info("Editor camera: Front view");
                }
                break;

            case GLFW_KEY_KP_3:
                if (m_editor && m_editor->getMode() == EditorMode::EDIT)
                {
                    m_editor->getEditorCamera()->setRightView();
                    Logger::info("Editor camera: Right view");
                }
                break;

            case GLFW_KEY_KP_7:
                if (m_editor && m_editor->getMode() == EditorMode::EDIT)
                {
                    m_editor->getEditorCamera()->setTopView();
                    Logger::info("Editor camera: Top view");
                }
                break;

            case GLFW_KEY_F:
                if (m_editor && m_editor->getMode() == EditorMode::EDIT)
                {
                    Scene* focusScene = m_sceneManager->getActiveScene();
                    Entity* selected = nullptr;
                    if (focusScene)
                    {
                        selected = m_editor->getSelection().getPrimaryEntity(*focusScene);
                    }
                    if (selected)
                    {
                        m_editor->getEditorCamera()->focusOn(selected->getWorldPosition());
                        Logger::info("Editor camera: Focus on '" + selected->getName() + "'");
                    }
                    else
                    {
                        m_editor->getEditorCamera()->focusOn(glm::vec3(0.0f, 0.5f, 0.0f));
                        Logger::info("Editor camera: Focus on scene center");
                    }
                }
                break;

            case GLFW_KEY_F12:
                if (m_editor)
                {
                    m_editor->getPerformancePanel().toggleOpen();
                    Logger::info(std::string("Performance panel: ")
                                 + (m_editor->getPerformancePanel().isOpen() ? "ON" : "OFF"));
                }
                break;
        }
    });

    // Subscribe to scroll for FOV zoom (gated by ImGui in edit mode)
    m_eventBus.subscribe<MouseScrollEvent>([this](const MouseScrollEvent& event)
    {
        if (m_editor && m_editor->wantCaptureMouse())
        {
            return;
        }
        m_camera->adjustFov(static_cast<float>(event.yOffset));
    });

    m_isRunning = true;
    Logger::info("Engine initialized successfully");
    Logger::info("Controls: Escape=toggle editor/play, WASD=move (play mode), Mouse=look (play mode), F1=wireframe, F2=tonemapper, F3=HDR debug, F4=POM, F5=bloom, F6=SSAO, F7=AA mode (None/MSAA/TAA/SMAA), F8=color grading, F9=CSM debug, F10=auto-exposure, F11=diagnostic capture, Ctrl+Q=quit");
    Logger::info("Editor camera: Alt+LMB=orbit, MMB=pan, Scroll=zoom, F=focus, Numpad 1/3/7=front/right/top");
    Logger::info("Gamepad: Left stick=move, Right stick=look, LB=sprint, Triggers=up/down");
    return true;
}

void Engine::run()
{
    Logger::info("Entering main loop...");

    while (m_isRunning)
    {
        // Handle window close (X button) — route through FileMenu for unsaved
        // changes check instead of quitting immediately.
        if (m_window->shouldClose())
        {
            glfwSetWindowShouldClose(m_window->getHandle(), GLFW_FALSE);
            if (m_editor)
            {
                m_editor->getFileMenu().requestQuit();
            }
            else
            {
                m_isRunning = false;
                break;
            }
        }

        // Check if FileMenu confirmed quit (after modal or if scene was clean)
        if (m_editor && m_editor->getFileMenu().shouldQuit())
        {
            m_isRunning = false;
            break;
        }

        // 1. Timer — calculate delta time
        float deltaTime = m_timer->update();

        // 2. Window — poll OS events
        m_window->pollEvents();

        // 3. Update input state (reset per-frame deltas)
        m_inputManager->update();

        // 3b. Process async texture uploads (GPU upload on main thread)
        m_resourceManager->processAsyncUploads();

        // 3c. Start ImGui frame early (so editor camera can read ImGui IO state)
        if (m_editor)
        {
            m_editor->prepareFrame();
        }

        // 3d. Resize render FBOs to match the target dimensions:
        // - Editor mode: match the viewport panel size (correct aspect ratio)
        // - Play mode: match the full window size
        bool editorActive = m_editor && m_editor->getMode() == EditorMode::EDIT;
        if (editorActive)
        {
            int vpW = 0;
            int vpH = 0;
            m_editor->getViewportSize(vpW, vpH);
            if (vpW > 0 && vpH > 0)
            {
                m_renderer->resizeRenderTarget(vpW, vpH);
                m_waterFbo.resize(vpW / 2, vpH / 2, vpW / 2, vpH / 2);
            }
        }
        else
        {
            // Play mode or no editor — render at full window size
            int ww = m_window->getWidth();
            int wh = m_window->getHeight();
            m_renderer->resizeRenderTarget(ww, wh);
            m_waterFbo.resize(ww / 2, wh / 2, ww / 2, wh / 2);
        }

        // Check for viewport clicks (uses previous frame's viewport bounds)
        if (editorActive)
        {
            m_editor->processViewportClick(
                m_renderer->getRenderWidth(), m_renderer->getRenderHeight());
        }

        // 3e. Update editor camera before rendering (uses previous frame's hover state)
        if (editorActive)
        {
            m_editor->updateEditorCamera(deltaTime);
            m_editor->applyEditorCamera(*m_camera);

            // 3f. Process brush tool input for environment painting
            BrushTool& brush = m_editor->getBrushTool();
            if (brush.isActive() && !m_editor->isGizmoActive())
            {
                int vpW = 0, vpH = 0;
                m_editor->getViewportSize(vpW, vpH);
                if (vpW > 0 && vpH > 0)
                {
                    ImGuiIO& brushIo = ImGui::GetIO();
                    float mouseX = (brushIo.MousePos.x - m_editor->getViewportMin().x)
                                 / static_cast<float>(vpW);
                    float mouseY = (brushIo.MousePos.y - m_editor->getViewportMin().y)
                                 / static_cast<float>(vpH);
                    float aspect = static_cast<float>(vpW) / static_cast<float>(vpH);

                    Ray mouseRay = BrushTool::createRay(*m_camera, mouseX, mouseY, aspect);
                    bool mouseDown = brushIo.MouseDown[0] && !brushIo.KeyAlt;
                    brush.processInput(mouseRay, mouseDown, deltaTime,
                                       m_foliageManager, m_editor->getCommandHistory());
                }
            }

            // 3g. Process terrain brush input for sculpting/painting
            TerrainBrush& terrainBrush = m_editor->getTerrainBrush();
            if (terrainBrush.isActive() && !m_editor->isGizmoActive()
                && m_terrain.isInitialized())
            {
                int vpW = 0, vpH = 0;
                m_editor->getViewportSize(vpW, vpH);
                if (vpW > 0 && vpH > 0)
                {
                    ImGuiIO& brushIo = ImGui::GetIO();
                    float mouseX = (brushIo.MousePos.x - m_editor->getViewportMin().x)
                                 / static_cast<float>(vpW);
                    float mouseY = (brushIo.MousePos.y - m_editor->getViewportMin().y)
                                 / static_cast<float>(vpH);
                    float aspect = static_cast<float>(vpW) / static_cast<float>(vpH);

                    Ray mouseRay = BrushTool::createRay(*m_camera, mouseX, mouseY, aspect);
                    bool mouseDown = brushIo.MouseDown[0] && !brushIo.KeyAlt;
                    terrainBrush.processInput(mouseRay, mouseDown, deltaTime,
                                              m_terrain, m_editor->getCommandHistory());
                }
            }
        }

        // 4. Scene — update entities and components
        m_sceneManager->update(deltaTime);

        // 5. Controller — process input and update camera
        Scene* activeScene = m_sceneManager->getActiveScene();
        if (activeScene)
        {
            activeScene->collectColliders(m_colliders);
        }
        else
        {
            m_colliders.clear();
        }
        m_controller->update(deltaTime, m_colliders);

        // 6. Renderer — draw the frame
        int winHeight = m_window->getHeight();
        if (winHeight <= 0)
        {
            m_window->pollEvents();
            continue;  // Skip rendering when minimized
        }

        m_profiler.beginFrame();
        m_renderer->beginFrame();

        // Use render target dimensions for aspect ratio (matches viewport panel in editor mode,
        // or window dimensions in play mode)
        int renderW = m_renderer->getRenderWidth();
        int renderH = m_renderer->getRenderHeight();
        if (renderH <= 0) renderH = 1;
        float aspectRatio = static_cast<float>(renderW) / static_cast<float>(renderH);

        if (activeScene)
        {
            activeScene->collectRenderData(m_renderData);

            // Sync foliage wind time for shadow pass (must match main foliage pass)
            m_renderer->setFoliageShadowTime(static_cast<float>(m_timer->getElapsedTime()));

            m_profiler.getGpuTimer().beginPass("Scene");
            m_renderer->renderScene(m_renderData, *m_camera, aspectRatio);
            m_profiler.getGpuTimer().endPass();

            // Render terrain (after opaques, before foliage/water)
            if (m_terrain.isInitialized())
            {
                m_profiler.getGpuTimer().beginPass("Terrain");
                m_terrainRenderer.render(m_terrain, *m_camera, aspectRatio, m_renderData,
                                         m_renderer->getCascadedShadowMap());
                m_profiler.getGpuTimer().endPass();
            }

            // Render foliage (after opaques, before water/particles)
            {
                glm::mat4 viewProj = m_camera->getProjectionMatrix(aspectRatio)
                                   * m_camera->getViewMatrix();
                auto visibleChunks = m_foliageManager.getVisibleChunks(viewProj);
                float elapsed = static_cast<float>(m_timer->getElapsedTime());
                if (!visibleChunks.empty())
                {
                    m_profiler.getGpuTimer().beginPass("Foliage");

                    // Pass shadow map and directional light to foliage for shadow receiving
                    CascadedShadowMap* csm = m_renderer->getCascadedShadowMap();
                    const DirectionalLight* dirLight =
                        m_renderData.hasDirectionalLight ? &m_renderData.directionalLight : nullptr;
                    m_foliageRenderer.render(visibleChunks, *m_camera, viewProj, elapsed,
                                             100.0f, csm, dirLight);
                    m_treeRenderer.render(visibleChunks, *m_camera, viewProj, elapsed);
                    m_profiler.getGpuTimer().endPass();
                }
            }

            // Render water surfaces (after opaques, before particles)
            if (!m_renderData.waterSurfaces.empty())
            {
                std::vector<WaterRenderItem> waterItems;
                waterItems.reserve(m_renderData.waterSurfaces.size());
                for (const auto& [comp, matrix] : m_renderData.waterSurfaces)
                {
                    waterItems.push_back({comp, matrix});
                }

                // Use the first water surface's world Y as the clip plane height
                float waterY = waterItems[0].worldMatrix[3][1];

                // Get directional light info for specular highlights
                glm::vec3 lightDir(0.0f, -1.0f, 0.0f);
                glm::vec3 lightColor(1.0f);
                if (m_renderData.hasDirectionalLight)
                {
                    lightDir = m_renderData.directionalLight.direction;
                    lightColor = m_renderData.directionalLight.diffuse;
                }

                float elapsed = static_cast<float>(m_timer->getElapsedTime());
                GLuint skyboxTex = m_renderer->getSkyboxTextureId();

                // Save renderer view state (geometryOnly passes overwrite m_lastProjection etc.)
                m_renderer->saveViewState();

                // --- Refraction pass: render scene below water into refraction FBO ---
                {
                    glm::vec4 refrClipPlane(0.0f, -1.0f, 0.0f, waterY + 0.1f);
                    glEnable(GL_CLIP_DISTANCE0);

                    m_waterFbo.bindRefraction();
                    m_renderer->renderScene(m_renderData, *m_camera, aspectRatio, refrClipPlane, true);

                    if (m_terrain.isInitialized())
                    {
                        m_terrainRenderer.render(m_terrain, *m_camera, aspectRatio, m_renderData,
                                                 m_renderer->getCascadedShadowMap(), refrClipPlane);
                    }

                    glDisable(GL_CLIP_DISTANCE0);
                }

                // --- Reflection pass: render scene above water with reflected camera ---
                {
                    glm::vec4 reflClipPlane(0.0f, 1.0f, 0.0f, -waterY + 0.1f);

                    // Create reflected camera: mirror position and pitch around water plane
                    Camera reflectedCamera = *m_camera;
                    glm::vec3 camPos = m_camera->getPosition();
                    float reflectedY = 2.0f * waterY - camPos.y;
                    reflectedCamera.setPosition(glm::vec3(camPos.x, reflectedY, camPos.z));
                    reflectedCamera.setPitch(-m_camera->getPitch());

                    glEnable(GL_CLIP_DISTANCE0);

                    m_waterFbo.bindReflection();
                    m_renderer->renderScene(m_renderData, reflectedCamera, aspectRatio, reflClipPlane, true);

                    if (m_terrain.isInitialized())
                    {
                        m_terrainRenderer.render(m_terrain, reflectedCamera, aspectRatio, m_renderData,
                                                 m_renderer->getCascadedShadowMap(), reflClipPlane);
                    }

                    // Note: foliage/trees are skipped in the reflection pass because
                    // the reflected camera is below the ground plane, which causes the
                    // foliage vertex shader's distance fade and wind to produce artifacts.

                    glDisable(GL_CLIP_DISTANCE0);
                }

                // Restore main scene FBO and view state after water passes
                m_renderer->rebindSceneFbo();
                m_renderer->restoreViewState();

                // Get reflection/refraction textures from water FBOs
                GLuint reflTex = m_waterFbo.getReflectionTexture();
                GLuint refrTex = m_waterFbo.getRefractionTexture();
                GLuint refrDepthTex = m_waterFbo.getRefractionDepthTexture();

                m_profiler.getGpuTimer().beginPass("Water");
                m_waterRenderer.render(waterItems, *m_camera, aspectRatio,
                                       elapsed, lightDir, lightColor, skyboxTex,
                                       reflTex, refrTex, refrDepthTex, 0.1f);
                m_profiler.getGpuTimer().endPass();
            }

            // Render particles (after scene transparent pass, before post-processing)
            if (!m_renderData.particleEmitters.empty())
            {
                glm::mat4 viewProj = m_camera->getProjectionMatrix(aspectRatio)
                                   * m_camera->getViewMatrix();
                GLuint depthTex = m_renderer->getResolvedDepthTexture();
                int particleW = m_renderer->getRenderWidth();
                int particleH = m_renderer->getRenderHeight();
                m_profiler.getGpuTimer().beginPass("Particles");
                m_particleRenderer.render(m_renderData.particleEmitters, *m_camera, viewProj,
                                          depthTex, particleW, particleH, 0.1f);
                m_profiler.getGpuTimer().endPass();
            }
        }

        // 7. Resolve MSAA, post-process, composite to output FBO
        m_profiler.getGpuTimer().beginPass("PostProcess");
        m_renderer->endFrame(deltaTime);
        m_profiler.getGpuTimer().endPass();

        // 7b. Selection system: ID buffer picking and outline rendering
        if (editorActive)
        {
            // Process pending pick request (renders ID buffer on demand)
            if (m_editor->isPickRequested())
            {
                m_renderer->renderIdBuffer(m_renderData, *m_camera, aspectRatio);
                int pickX = 0;
                int pickY = 0;
                m_editor->getPickCoords(pickX, pickY);
                uint32_t entityId = m_renderer->pickEntityAt(pickX, pickY);
                m_editor->handlePickResult(entityId);

                // Log the selection result
                if (entityId != 0 && activeScene)
                {
                    Entity* picked = activeScene->findEntityById(entityId);
                    if (picked)
                    {
                        Logger::info("Selected: '" + picked->getName() + "' (ID " + std::to_string(entityId) + ")");
                    }
                }
                else if (entityId == 0)
                {
                    Logger::info("Selection cleared (clicked background)");
                }
            }

            // Render selection outlines into the output FBO
            if (m_editor->getSelection().hasSelection())
            {
                m_renderer->renderSelectionOutline(
                    m_renderData, m_editor->getSelection().getSelectedIds(),
                    *m_camera, aspectRatio);
            }

            // 7c. Queue light gizmos for selected entities, then flush debug draw
            if (activeScene)
            {
                drawLightGizmos(*activeScene, m_editor->getSelection());
            }
            m_renderer->bindOutputFbo();
            glm::mat4 vp = m_camera->getProjectionMatrix(aspectRatio)
                         * m_camera->getViewMatrix();
            m_debugDraw.flush(vp);

            // 7d. Render brush preview circle (foliage brush)
            {
                BrushTool& brush = m_editor->getBrushTool();
                glm::vec3 hitPoint, hitNormal;
                if (brush.getHitPoint(hitPoint, hitNormal))
                {
                    bool isEraser = (brush.mode == BrushTool::Mode::ERASER);
                    m_editor->getBrushPreview().render(
                        hitPoint, hitNormal, brush.radius, vp, isEraser);
                }
            }

            // 7e. Render terrain brush preview circle
            {
                TerrainBrush& tBrush = m_editor->getTerrainBrush();
                glm::vec3 hitPoint, hitNormal;
                if (tBrush.getHitPoint(hitPoint, hitNormal))
                {
                    bool isEraser = (tBrush.mode == TerrainBrushMode::LOWER);
                    m_editor->getBrushPreview().render(
                        hitPoint, hitNormal, tBrush.radius, vp, isEraser);
                }
            }
        }

        // 8. Display the rendered frame
        if (editorActive)
        {
            // Editor mode: clear screen to dark grey, then ImGui draws the scene
            // inside the Viewport panel as a texture. No blit to screen.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, m_window->getWidth(), m_window->getHeight());
            glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            m_editor->drawPanels(m_renderer.get(), activeScene, m_camera.get());
            m_editor->endFrame();
        }
        else
        {
            // Play mode: blit output directly to screen, run ImGui frame (hidden)
            m_renderer->blitToScreen();
            if (m_editor)
            {
                m_editor->drawPanels(nullptr, nullptr);
                m_editor->endFrame();
            }
        }

        // 9. Window — swap buffers (flushes GPU work, making query results available)
        m_window->swapBuffers();

        // 10. End profiler frame (collect GPU results after swap ensures queries are complete)
        m_profiler.endFrame(deltaTime);
    }

    Logger::info("Main loop ended");
}

void Engine::shutdown()
{
    if (!m_isRunning && !m_window)
    {
        return;
    }

    Logger::info("Shutting down engine...");

    // Save window position/size before destroying anything
    if (m_window)
    {
        m_window->saveWindowState();
    }

    // Clear foliage shadow pointers before destroying the foliage renderer
    if (m_renderer)
    {
        m_renderer->setFoliageShadowCaster(nullptr, nullptr);
    }

    // Shut down all subsystems that hold GL resources BEFORE destroying the window
    // (the window owns the GL context — GL calls after window destruction crash).
    m_terrain.shutdown();
    m_terrainRenderer.shutdown();
    m_foliageRenderer.shutdown();
    m_treeRenderer.shutdown();
    m_waterFbo.shutdown();
    m_waterRenderer.shutdown();
    m_particleRenderer.shutdown();
    m_profiler.shutdown();
    m_debugDraw.cleanup();
    m_editor.reset();
    m_controller.reset();
    m_camera.reset();
    m_sceneManager.reset();
    m_resourceManager.reset();
    m_renderer.reset();
    m_inputManager.reset();
    m_timer.reset();
    m_window.reset();
    m_eventBus.clearAll();

    m_isRunning = false;
    Logger::info("Engine shutdown complete");
}

void Engine::drawLightGizmos(Scene& scene, const Selection& selection)
{
    if (!selection.hasSelection())
    {
        return;
    }

    const auto& selectedIds = selection.getSelectedIds();
    for (uint32_t id : selectedIds)
    {
        Entity* entity = scene.findEntityById(id);
        if (!entity)
        {
            continue;
        }

        glm::vec3 worldPos = entity->getWorldPosition();

        // Directional light: 3 parallel arrows showing direction
        if (auto* dirComp = entity->getComponent<DirectionalLightComponent>())
        {
            glm::vec3 color = dirComp->light.diffuse;
            glm::vec3 dir = glm::normalize(dirComp->light.direction);
            float arrowLen = 2.0f;
            float spacing = 0.3f;

            // Build perpendicular vectors for offset arrows
            glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                           ? glm::vec3(1.0f, 0.0f, 0.0f)
                           : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::normalize(glm::cross(dir, up));
            glm::vec3 fwd = glm::cross(right, dir);

            // Center arrow
            DebugDraw::arrow(worldPos, worldPos + dir * arrowLen, color);
            // Offset arrows
            DebugDraw::arrow(worldPos + right * spacing,
                             worldPos + right * spacing + dir * arrowLen, color);
            DebugDraw::arrow(worldPos - right * spacing,
                             worldPos - right * spacing + dir * arrowLen, color);
            DebugDraw::arrow(worldPos + fwd * spacing,
                             worldPos + fwd * spacing + dir * arrowLen, color);
            DebugDraw::arrow(worldPos - fwd * spacing,
                             worldPos - fwd * spacing + dir * arrowLen, color);
        }

        // Point light: wireframe sphere at effective range
        if (auto* ptComp = entity->getComponent<PointLightComponent>())
        {
            glm::vec3 color = ptComp->light.diffuse;
            float range = calculateLightRange(ptComp->light.constant,
                                              ptComp->light.linear,
                                              ptComp->light.quadratic);
            range = std::min(range, 200.0f); // clamp to avoid absurd gizmos
            DebugDraw::wireSphere(worldPos, range, color);
        }

        // Spot light: cone wireframe
        if (auto* spotComp = entity->getComponent<SpotLightComponent>())
        {
            glm::vec3 color = spotComp->light.diffuse;
            float range = calculateLightRange(spotComp->light.constant,
                                              spotComp->light.linear,
                                              spotComp->light.quadratic);
            range = std::min(range, 200.0f);

            float outerAngleDeg = glm::degrees(
                std::acos(std::clamp(spotComp->light.outerCutoff, -1.0f, 1.0f)));
            DebugDraw::cone(worldPos, spotComp->light.direction,
                            range, outerAngleDeg, color);

            // Inner cone (dimmer, to show soft edge)
            float innerAngleDeg = glm::degrees(
                std::acos(std::clamp(spotComp->light.innerCutoff, -1.0f, 1.0f)));
            glm::vec3 dimColor = color * 0.4f;
            DebugDraw::cone(worldPos, spotComp->light.direction,
                            range, innerAngleDeg, dimColor, 4);
        }
    }
}

void Engine::setupDemoScene()
{
    Logger::info("Setting up demo scene...");

    Scene* scene = m_sceneManager->createScene("Demo");

    // --- Create shared resources via ResourceManager ---
    auto cubeMesh = m_resourceManager->getCubeMesh();
    auto planeMesh = m_resourceManager->getPlaneMesh(30.0f);

    // --- PBR materials ---

    auto groundMat = m_resourceManager->createMaterial("ground");
    groundMat->setType(MaterialType::PBR);
    groundMat->setAlbedo(glm::vec3(1.0f));
    groundMat->setMetallic(0.0f);
    groundMat->setRoughness(0.9f);
    groundMat->setDiffuseTexture(m_resourceManager->loadTexture(
        "assets/textures/everytexture-com-stock-rocks-texture-00038-2048/everytexture.com-stock-rocks-texture-00038-diffuse-2048.jpg"));
    groundMat->setNormalMap(m_resourceManager->loadTexture(
        "assets/textures/everytexture-com-stock-rocks-texture-00038-2048/everytexture.com-stock-rocks-texture-00038-normal-2048.jpg", true));
    groundMat->setStochasticTiling(true);
    groundMat->setUvScale(6.0f);

    // Block 1 — Red Brick (left) — sealed/glazed brick with clearcoat
    auto redBrickMat = m_resourceManager->createMaterial("red_brick");
    redBrickMat->setType(MaterialType::PBR);
    redBrickMat->setAlbedo(glm::vec3(1.0f));
    redBrickMat->setMetallic(0.0f);
    redBrickMat->setRoughness(1.0f);
    redBrickMat->setClearcoat(0.5f);
    redBrickMat->setClearcoatRoughness(0.1f);
    redBrickMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/red_brick_diff_2k.jpg"));
    redBrickMat->setNormalMap(m_resourceManager->loadTexture("assets/textures/red_brick_nor_gl_2k.jpg", true));
    redBrickMat->setMetallicRoughnessTexture(
        m_resourceManager->loadTexture("assets/textures/red_brick_rough_2k.jpg", true));
    redBrickMat->setUvScale(0.5f);

    // Block 2 — Gold (center)
    auto goldMat = m_resourceManager->createMaterial("gold");
    goldMat->setType(MaterialType::PBR);
    goldMat->setAlbedo(glm::vec3(1.4f, 1.1f, 0.5f));
    goldMat->setMetallic(1.0f);
    goldMat->setRoughness(0.25f);
    goldMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/Texturelabs_Metal_124M.jpg"));

    // Block 3 — Wood (right)
    auto woodMat = m_resourceManager->createMaterial("wood");
    woodMat->setType(MaterialType::PBR);
    woodMat->setAlbedo(glm::vec3(1.0f));
    woodMat->setMetallic(0.0f);
    woodMat->setRoughness(0.9f);
    woodMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/Texturelabs_Glass_120M.jpg"));

    // Block 4 — Rough Brick (back)
    auto roughBrickMat = m_resourceManager->createMaterial("rough_brick");
    roughBrickMat->setType(MaterialType::PBR);
    roughBrickMat->setAlbedo(glm::vec3(1.0f));
    roughBrickMat->setMetallic(0.0f);
    roughBrickMat->setRoughness(0.9f);
    roughBrickMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/brick_wall_005_diff_2k.jpg"));
    roughBrickMat->setNormalMap(m_resourceManager->loadTexture("assets/textures/brick_wall_005_nor_gl_2k.jpg", true));

    // --- Height maps for POM ---

    // Ground — rock bump/height map
    auto groundHeightTex = std::make_shared<Texture>();
    if (groundHeightTex->loadFromFile(
        "assets/textures/everytexture-com-stock-rocks-texture-00038-2048/everytexture.com-stock-rocks-texture-00038-bump-2048.jpg", true))
    {
        groundMat->setHeightMap(groundHeightTex);
        groundMat->setHeightScale(0.06f);
    }

    // Block 1 — embossed label "1" over red brick displacement
    auto redBrickHeightTex = std::make_shared<Texture>();
    if (redBrickHeightTex->loadFromFile("assets/textures/red_brick_disp_2k.jpg", true))
    {
        redBrickMat->setHeightMap(redBrickHeightTex);
        redBrickMat->setHeightScale(0.05f);
    }

    // Block 2 — normal map label "2" (generated from height map, visible from all angles)
    auto goldLabelNormal = Texture::generateNormalFromHeight("assets/textures/label_2.png", 12.0f);
    if (goldLabelNormal)
    {
        goldMat->setNormalMap(goldLabelNormal);
    }

    // Block 3 — normal map label "3" (generated from height map, visible from all angles)
    auto woodLabelNormal = Texture::generateNormalFromHeight("assets/textures/label_3.png", 12.0f);
    if (woodLabelNormal)
    {
        woodMat->setNormalMap(woodLabelNormal);
    }

    // Block 4 — brick wall displacement (deeper to show off self-shadowing)
    auto roughBrickHeightTex = std::make_shared<Texture>();
    if (roughBrickHeightTex->loadFromFile("assets/textures/brick_wall_005_disp_2k.jpg", true))
    {
        roughBrickMat->setHeightMap(roughBrickHeightTex);
        roughBrickMat->setHeightScale(0.08f);
    }

    // --- Ground ---
    Entity* ground = scene->createEntity("Ground");
    ground->transform.position = glm::vec3(0.0f, 0.02f, 0.0f);  // Slightly above terrain in flat zone
    auto* groundMR = ground->addComponent<MeshRenderer>(planeMesh, groundMat);
    // Ground only receives shadows, doesn't cast them (nothing is below it).
    // This prevents self-shadowing acne that causes visible cascade boundaries.
    groundMR->setCastsShadow(false);

    // --- Cubes ---
    Entity* redBrickCube = scene->createEntity("1 Red Brick");
    redBrickCube->transform.position = glm::vec3(-3.0f, 0.5f, -1.0f);
    redBrickCube->transform.rotation = glm::vec3(0.0f, 30.0f, 0.0f);
    auto* redBrickRenderer = redBrickCube->addComponent<MeshRenderer>(cubeMesh, redBrickMat);
    redBrickRenderer->setBounds(AABB::unitCube());

    Entity* goldCube = scene->createEntity("2 Gold");
    goldCube->transform.position = glm::vec3(0.0f, 0.5f, 0.0f);
    auto* goldRenderer = goldCube->addComponent<MeshRenderer>(cubeMesh, goldMat);
    goldRenderer->setBounds(AABB::unitCube());

    Entity* woodCube = scene->createEntity("3 Wood");
    woodCube->transform.position = glm::vec3(3.0f, 0.75f, -1.0f);
    woodCube->transform.scale = glm::vec3(1.0f, 1.5f, 1.0f);
    auto* woodRenderer = woodCube->addComponent<MeshRenderer>(cubeMesh, woodMat);
    woodRenderer->setBounds(AABB::unitCube());

    Entity* roughBrickCube = scene->createEntity("4 Rough Brick");
    roughBrickCube->transform.position = glm::vec3(0.0f, 1.0f, -5.0f);
    roughBrickCube->transform.scale = glm::vec3(2.0f, 2.0f, 2.0f);
    roughBrickCube->transform.rotation = glm::vec3(0.0f, 45.0f, 0.0f);
    auto* roughBrickRenderer = roughBrickCube->addComponent<MeshRenderer>(cubeMesh, roughBrickMat);
    roughBrickRenderer->setBounds(AABB::unitCube());

    // --- Glass cube (transparent) ---
    auto glassMat = m_resourceManager->createMaterial("glass");
    glassMat->setType(MaterialType::PBR);
    glassMat->setAlbedo(glm::vec3(0.4f, 0.6f, 0.9f));
    glassMat->setMetallic(0.0f);
    glassMat->setRoughness(0.1f);
    glassMat->setAlphaMode(AlphaMode::BLEND);
    glassMat->setBaseColorAlpha(0.3f);

    Entity* glassCube = scene->createEntity("5 Glass");
    glassCube->transform.position = glm::vec3(1.5f, 0.5f, 2.0f);
    glassCube->transform.rotation = glm::vec3(0.0f, 15.0f, 0.0f);
    auto* glassRenderer = glassCube->addComponent<MeshRenderer>(cubeMesh, glassMat);
    glassRenderer->setBounds(AABB::unitCube());

    // --- Emissive cube (lava glow) ---
    auto lavaMat = m_resourceManager->createMaterial("lava");
    lavaMat->setType(MaterialType::PBR);
    lavaMat->setAlbedo(glm::vec3(0.1f, 0.02f, 0.0f));
    lavaMat->setMetallic(0.0f);
    lavaMat->setRoughness(0.8f);
    lavaMat->setEmissive(glm::vec3(1.0f, 0.3f, 0.05f));
    lavaMat->setEmissiveStrength(5.0f);

    Entity* lavaCube = scene->createEntity("6 Lava");
    lavaCube->transform.position = glm::vec3(-1.5f, 0.35f, 2.5f);
    lavaCube->transform.scale = glm::vec3(0.7f);
    lavaCube->transform.rotation = glm::vec3(0.0f, 30.0f, 0.0f);
    auto* lavaRenderer = lavaCube->addComponent<MeshRenderer>(cubeMesh, lavaMat);
    lavaRenderer->setBounds(AABB::unitCube());

    // Emissive light component — auto-generates a point light from the emissive material
    auto* emissiveLC = lavaCube->addComponent<EmissiveLightComponent>();
    emissiveLC->lightRadius = 4.0f;
    emissiveLC->lightIntensity = 0.8f;

    // --- Lights ---
    Entity* sun = scene->createEntity("Sun");
    auto* dirLight = sun->addComponent<DirectionalLightComponent>();
    dirLight->light.direction = glm::vec3(-0.3f, -0.8f, -0.5f);
    dirLight->light.ambient = glm::vec3(0.15f, 0.15f, 0.18f);
    dirLight->light.diffuse = glm::vec3(1.8f, 1.7f, 1.5f);
    dirLight->light.specular = glm::vec3(1.0f);

    Entity* warmLight = scene->createEntity("Warm Light");
    warmLight->transform.position = glm::vec3(1.5f, 2.0f, 1.5f);
    auto* warmPL = warmLight->addComponent<PointLightComponent>();
    warmPL->light.ambient = glm::vec3(0.02f, 0.02f, 0.01f);
    warmPL->light.diffuse = glm::vec3(0.9f, 0.7f, 0.3f);
    warmPL->light.specular = glm::vec3(1.0f, 0.9f, 0.6f);
    warmPL->light.linear = 0.14f;
    warmPL->light.quadratic = 0.07f;
    warmPL->light.castsShadow = true;

    Entity* coolLight = scene->createEntity("Cool Light");
    coolLight->transform.position = glm::vec3(-2.0f, 2.5f, 2.0f);
    auto* coolPL = coolLight->addComponent<PointLightComponent>();
    coolPL->light.ambient = glm::vec3(0.01f, 0.01f, 0.02f);
    coolPL->light.diffuse = glm::vec3(0.3f, 0.5f, 0.9f);
    coolPL->light.specular = glm::vec3(0.5f, 0.7f, 1.0f);
    coolPL->light.linear = 0.14f;
    coolPL->light.quadratic = 0.07f;
    coolPL->light.castsShadow = true;

    // --- New primitive shapes (5B-1 test) ---
    auto sphereMesh = m_resourceManager->getSphereMesh();
    auto cylinderMesh = m_resourceManager->getCylinderMesh();
    auto coneMesh = m_resourceManager->getConeMesh();
    auto wedgeMesh = m_resourceManager->getWedgeMesh();

    auto primitiveMat = m_resourceManager->createMaterial("primitive_default");
    primitiveMat->setType(MaterialType::PBR);
    primitiveMat->setAlbedo(glm::vec3(0.7f));
    primitiveMat->setMetallic(0.0f);
    primitiveMat->setRoughness(0.5f);

    Entity* sphereEntity = scene->createEntity("7 Sphere");
    sphereEntity->transform.position = glm::vec3(-4.5f, 0.52f, 2.0f);
    auto* sphereMR = sphereEntity->addComponent<MeshRenderer>(sphereMesh, primitiveMat);
    sphereMR->setBounds(sphereMesh->getLocalBounds());

    Entity* cylinderEntity = scene->createEntity("8 Cylinder");
    cylinderEntity->transform.position = glm::vec3(-6.0f, 0.5f, 0.0f);
    auto* cylinderMR = cylinderEntity->addComponent<MeshRenderer>(cylinderMesh, primitiveMat);
    cylinderMR->setBounds(cylinderMesh->getLocalBounds());

    Entity* coneEntity = scene->createEntity("9 Cone");
    coneEntity->transform.position = glm::vec3(-6.0f, 0.5f, 2.0f);
    auto* coneMR = coneEntity->addComponent<MeshRenderer>(coneMesh, primitiveMat);
    coneMR->setBounds(coneMesh->getLocalBounds());

    Entity* wedgeEntity = scene->createEntity("10 Wedge");
    wedgeEntity->transform.position = glm::vec3(-4.5f, 0.5f, 4.0f);
    auto* wedgeMR = wedgeEntity->addComponent<MeshRenderer>(wedgeMesh, primitiveMat);
    wedgeMR->setBounds(wedgeMesh->getLocalBounds());

    // --- Optional glTF model loading ---
    auto testModel = m_resourceManager->loadModel("assets/models/test_model.glb");
    if (testModel)
    {
        Entity* modelRoot = testModel->instantiate(*scene, nullptr, "TestModel");
        modelRoot->transform.position = glm::vec3(5.0f, 0.0f, -3.0f);
        Logger::info("Loaded glTF model: " + std::to_string(testModel->getMeshCount()) + " meshes");
    }

    // --- Hill with water basin ---
    // Outer hill ring (flattened cylinders arranged as a rim)
    auto hillMat = m_resourceManager->createMaterial("hill");
    hillMat->setType(MaterialType::PBR);
    hillMat->setAlbedo(glm::vec3(0.35f, 0.45f, 0.2f));  // Earthy green
    hillMat->setMetallic(0.0f);
    hillMat->setRoughness(0.95f);

    glm::vec3 hillCenter(5.0f, 0.0f, 4.0f);
    float hillRadius = 3.8f;
    float hillHeight = 0.6f;
    int rimSegments = 12;
    for (int i = 0; i < rimSegments; ++i)
    {
        float angle = static_cast<float>(i) * 2.0f * glm::pi<float>() / static_cast<float>(rimSegments);
        float x = hillCenter.x + hillRadius * std::cos(angle);
        float z = hillCenter.z + hillRadius * std::sin(angle);

        std::string name = "Hill Rim " + std::to_string(i);
        Entity* rim = scene->createEntity(name);
        rim->transform.position = glm::vec3(x, hillHeight * 0.5f, z);
        rim->transform.scale = glm::vec3(2.0f, hillHeight, 2.0f);
        rim->transform.rotation = glm::vec3(0.0f, -angle * 180.0f / glm::pi<float>(), 0.0f);
        auto* rimMR = rim->addComponent<MeshRenderer>(cubeMesh, hillMat);
        rimMR->setBounds(AABB::unitCube());
    }

    // Hill floor (flat disc under the water)
    auto basinMat = m_resourceManager->createMaterial("basin_floor");
    basinMat->setType(MaterialType::PBR);
    basinMat->setAlbedo(glm::vec3(0.25f, 0.2f, 0.15f));  // Dark muddy
    basinMat->setMetallic(0.0f);
    basinMat->setRoughness(1.0f);

    auto basinFloor = m_resourceManager->getPlaneMesh(6.0f);
    Entity* floor = scene->createEntity("Basin Floor");
    floor->transform.position = glm::vec3(hillCenter.x, 0.04f, hillCenter.z);
    auto* floorMR = floor->addComponent<MeshRenderer>(basinFloor, basinMat);
    floorMR->setCastsShadow(false);

    // Water surface sitting in the basin
    Entity* waterEntity = scene->createEntity("Water Pool");
    waterEntity->transform.position = glm::vec3(hillCenter.x, hillHeight * 0.65f, hillCenter.z);
    {
        auto* water = waterEntity->addComponent<WaterSurfaceComponent>();
        auto& wCfg = water->getConfig();
        wCfg.width = 5.5f;
        wCfg.depth = 5.5f;
        wCfg.gridResolution = 32;
        wCfg.numWaves = 3;
        wCfg.waves[0] = {0.005f, 3.0f, 0.2f, 10.0f};
        wCfg.waves[1] = {0.003f, 2.0f, 0.15f, 75.0f};
        wCfg.waves[2] = {0.002f, 1.5f, 0.25f, 140.0f};
        wCfg.shallowColor = {0.1f, 0.4f, 0.5f, 0.8f};
        wCfg.deepColor = {0.02f, 0.1f, 0.25f, 1.0f};
        wCfg.flowSpeed = 0.15f;
        wCfg.specularPower = 256.0f;
        wCfg.dudvStrength = 0.015f;
        wCfg.normalStrength = 0.8f;
    }

    // --- Demo foliage (10K grass instances around the ground plane) ---
    {
        FoliageTypeConfig grassConfig;
        grassConfig.name = "Short Grass";
        grassConfig.minScale = 0.6f;
        grassConfig.maxScale = 1.3f;
        grassConfig.tintVariation = glm::vec3(0.12f, 0.15f, 0.05f);

        // Paint overlapping stamps across the ground plane for dense coverage
        const float groundHalf = 15.0f;  // Ground plane is 30m
        for (float x = -groundHalf + 1.0f; x < groundHalf - 1.0f; x += 3.0f)
        {
            for (float z = -groundHalf + 1.0f; z < groundHalf - 1.0f; z += 3.0f)
            {
                m_foliageManager.paintFoliage(
                    0, glm::vec3(x, 0.0f, z), 3.0f, 6.0f, 0.3f, grassConfig);
            }
        }
        // Clear grass around scene objects so it doesn't poke through
        struct Exclusion { glm::vec3 pos; float radius; };
        Exclusion exclusions[] = {
            {{-3.0f, 0.0f, -1.0f}, 1.2f},   // Red Brick cube
            {{ 0.0f, 0.0f,  0.0f}, 1.2f},   // Gold cube
            {{ 3.0f, 0.0f, -1.0f}, 1.2f},   // Wood cube
            {{ 0.0f, 0.0f, -5.0f}, 2.2f},   // Rough Brick cube (2x scale)
            {{ 1.5f, 0.0f,  2.0f}, 1.2f},   // Glass cube
            {{-1.5f, 0.0f,  2.5f}, 1.0f},   // Lava cube
            {{-4.5f, 0.0f,  2.0f}, 0.8f},   // Sphere
            {{-6.0f, 0.0f,  0.0f}, 0.8f},   // Cylinder
            {{-6.0f, 0.0f,  2.0f}, 0.8f},   // Cone
            {{-4.5f, 0.0f,  4.0f}, 0.8f},   // Wedge
            {{ 5.0f, 0.0f, -3.0f}, 2.0f},   // glTF model
            {{ 5.0f, 0.0f,  4.0f}, 6.0f},   // Hill basin + water
        };
        for (const auto& ex : exclusions)
        {
            m_foliageManager.eraseAllFoliage(ex.pos, ex.radius);
        }

        Logger::info("Demo foliage: " + std::to_string(m_foliageManager.getTotalFoliageCount())
                     + " grass instances across " + std::to_string(m_foliageManager.getChunkCount()) + " chunks");
    }

    // Initial scene update to compute world matrices
    scene->update(0.0f);

    Logger::info("Demo scene ready: entities with components, 1 directional + 2 point lights + emissive lava + glass cube + foliage");
}

} // namespace Vestige
