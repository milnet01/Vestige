// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file engine.cpp
/// @brief Engine implementation — main loop and subsystem orchestration.
#include "core/engine.h"
#include "core/logger.h"
#include "physics/cloth_component.h"
#include "systems/atmosphere_system.h"
#include "systems/particle_system.h"
#include "systems/water_system.h"
#include "systems/vegetation_system.h"
#include "systems/terrain_system.h"
#include "systems/cloth_system.h"
#include "systems/destruction_system.h"
#include "systems/character_system.h"
#include "systems/lighting_system.h"
#include "systems/audio_system.h"
#include "systems/ui_system.h"
#include "systems/navigation_system.h"
#include "renderer/particle_renderer.h"
#include "renderer/water_renderer.h"
#include "renderer/water_fbo.h"
#include "renderer/foliage_renderer.h"
#include "renderer/tree_renderer.h"
#include "renderer/terrain_renderer.h"
#include "environment/environment_forces.h"
#include "environment/foliage_manager.h"
#include "environment/terrain.h"
#include "physics/physics_character_controller.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include "renderer/radiosity_baker.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "scene/water_surface.h"
#include "scene/particle_emitter.h"
#include "scene/particle_presets.h"
#include "resource/model.h"
#include "renderer/frame_diagnostics.h"
#include "renderer/debug_draw.h"
#include "renderer/light_utils.h"
#include "editor/tools/brush_tool.h"
#include "profiler/cpu_profiler.h"

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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
    Logger::openLogFile("logs");
    Logger::info("=== Vestige Engine v0.5.0 ===");
    Logger::info("Initializing engine...");

    m_assetPath = config.assetPath;

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

    // Initialize editor (ImGui + docking) — must come after InputManager for callback chaining
    m_editor = std::make_unique<Editor>();
    if (!m_editor->initialize(m_window->getHandle(), config.assetPath))
    {
        Logger::warning("Editor initialization failed — continuing without editor");
        m_editor.reset();
    }

    // Sync editor camera with the scene's initial camera position
    if (m_editor && m_editor->getEditorCamera())
    {
        m_editor->getEditorCamera()->syncFromCamera(*m_camera);
    }

    // Initialize debug draw system for editor overlays (light gizmos, etc.)
    if (!m_debugDraw.initialize(config.assetPath))
    {
        Logger::warning("DebugDraw initialization failed — gizmos will be unavailable");
    }

    // Initialize physics world (shared infrastructure — used by multiple domain systems)
    if (!m_physicsWorld.initialize())
    {
        Logger::warning("PhysicsWorld initialization failed — physics will be unavailable");
    }

    // Initialize performance profiler
    m_profiler.init();

    // Register domain systems (order = update order)
    auto* atmoSys    = m_systemRegistry.registerSystem<AtmosphereSystem>();
    auto* particleSys = m_systemRegistry.registerSystem<ParticleVfxSystem>();
    auto* waterSys   = m_systemRegistry.registerSystem<WaterSystem>();
    auto* vegSys     = m_systemRegistry.registerSystem<VegetationSystem>();
    auto* terrainSys = m_systemRegistry.registerSystem<TerrainSystem>();
    m_systemRegistry.registerSystem<ClothSystem>();
    m_systemRegistry.registerSystem<DestructionSystem>();
    auto* charSys    = m_systemRegistry.registerSystem<CharacterSystem>();
    m_systemRegistry.registerSystem<LightingSystem>();

    // Phase 9C: new domain systems
    m_systemRegistry.registerSystem<AudioSystem>();
    auto* uiSys = m_systemRegistry.registerSystem<UISystem>();
    m_systemRegistry.registerSystem<NavigationSystem>();
    m_uiSystem = uiSys;

    // Cache raw pointers for hot-path render loop access
    m_environmentForces    = &atmoSys->getEnvironmentForces();
    m_particleRenderer     = &particleSys->getParticleRenderer();
    m_waterRenderer        = &waterSys->getWaterRenderer();
    m_waterFbo             = &waterSys->getWaterFbo();
    m_foliageManager       = &vegSys->getFoliageManager();
    m_foliageRenderer      = &vegSys->getFoliageRenderer();
    m_treeRenderer         = &vegSys->getTreeRenderer();
    m_terrain              = &terrainSys->getTerrain();
    m_terrainRenderer      = &terrainSys->getTerrainRenderer();
    m_physicsCharController = &charSys->getPhysicsCharController();

    // Set up the scene (after cached pointers are set — scene setup uses m_environmentForces etc.)
    setupTabernacleScene();

    // Create static physics bodies from scene geometry (for physics character controller)
    createPhysicsStaticBodies();

    // Wire up foliage shadow casting into the renderer's shadow pass
    m_renderer->setFoliageShadowCaster(m_foliageRenderer, m_foliageManager);

    // Give the editor access to the resource manager and foliage manager
    if (m_editor)
    {
        m_editor->setResourceManager(m_resourceManager.get());
        m_editor->setFoliageManager(m_foliageManager);
        m_editor->setTerrain(m_terrain);
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
                if (m_editor) m_editor->showNotification(std::string("Bloom: ") + (m_renderer->isBloomEnabled() ? "ON" : "OFF"));
                break;

            case GLFW_KEY_F6:
                m_renderer->setSsaoEnabled(!m_renderer->isSsaoEnabled());
                Logger::info(std::string("SSAO: ") + (m_renderer->isSsaoEnabled() ? "ON" : "OFF"));
                if (m_editor) m_editor->showNotification(std::string("SSAO: ") + (m_renderer->isSsaoEnabled() ? "ON" : "OFF"));
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
                if (m_editor) m_editor->showNotification(std::string("CSM debug: ") + (debug ? "ON" : "OFF"));
                break;
            }

            case GLFW_KEY_F10:
                m_renderer->setAutoExposure(!m_renderer->isAutoExposure());
                Logger::info(std::string("Auto-exposure: ") + (m_renderer->isAutoExposure() ? "ON" : "OFF"));
                if (m_editor) m_editor->showNotification(std::string("Auto-exposure: ") + (m_renderer->isAutoExposure() ? "ON" : "OFF"));
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

            case GLFW_KEY_G:
            {
                bool walk = !m_controller->isWalkMode();
                m_controller->setWalkMode(walk);
                if (m_physicsCharController->isInitialized())
                {
                    m_physicsCharController->setFlyMode(!walk);
                }
                Logger::info(std::string("Movement: ") + (walk ? "Walk" : "Fly"));
                if (m_editor) m_editor->showNotification(
                    std::string("Movement: ") + (walk ? "Walk" : "Fly"));
                break;
            }

            case GLFW_KEY_P:
            {
                if (m_physicsCharController->isInitialized())
                {
                    m_usePhysicsController = !m_usePhysicsController;
                    if (m_usePhysicsController)
                    {
                        // Sync camera position to physics controller
                        glm::vec3 feet = m_camera->getPosition();
                        feet.y -= m_physicsCharController->getConfig().eyeHeight;
                        m_physicsCharController->setPosition(feet);
                    }
                    std::string mode = m_usePhysicsController ? "Physics (Jolt)" : "Legacy (AABB)";
                    Logger::info("Controller: " + mode);
                    if (m_editor) m_editor->showNotification("Controller: " + mode);
                }
                break;
            }

            case GLFW_KEY_E:
            {
                // Interact with physics objects — raycast + impulse
                if (m_physicsWorld.isInitialized() && m_isCursorCaptured)
                {
                    glm::vec3 origin = m_camera->getPosition();
                    glm::vec3 dir = m_camera->getFront();
                    float interactRange = 3.0f;
                    float interactForce = 5.0f;

                    JPH::BodyID hitBody;
                    float fraction = 0.0f;
                    if (m_physicsWorld.rayCast(origin, dir * interactRange, hitBody, fraction))
                    {
                        if (m_physicsWorld.getBodyMotionType(hitBody) == JPH::EMotionType::Dynamic)
                        {
                            glm::vec3 hitPoint = origin + dir * fraction * interactRange;
                            m_physicsWorld.applyImpulseAtPoint(hitBody, dir * interactForce, hitPoint);
                        }
                    }
                }
                break;
            }

            case GLFW_KEY_V:
            {
                // Cycle frame cap: Uncapped → 60 → VSync → Uncapped
                int currentCap = m_timer->getFrameRateCap();
                if (currentCap == 0 && !m_window->isVsyncEnabled())
                {
                    m_timer->setFrameRateCap(60);
                    m_window->setVsync(false);
                    Logger::info("Frame rate: capped at 60 FPS");
                    if (m_editor) m_editor->showNotification("Frame rate: 60 FPS cap");
                }
                else if (currentCap == 60)
                {
                    m_timer->setFrameRateCap(0);
                    m_window->setVsync(true);
                    Logger::info("Frame rate: VSync ON");
                    if (m_editor) m_editor->showNotification("Frame rate: VSync");
                }
                else
                {
                    m_timer->setFrameRateCap(0);
                    m_window->setVsync(false);
                    Logger::info("Frame rate: uncapped");
                    if (m_editor) m_editor->showNotification("Frame rate: Uncapped");
                }
                break;
            }
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

    m_visualTestMode = config.visualTestMode;
    if (m_visualTestMode)
    {
        setupVisualTestViewpoints();
        Logger::info("Visual test mode enabled: "
                     + std::to_string(m_visualTestRunner.viewpointCount()) + " viewpoints");
    }

    // Initialize all registered domain systems
    if (!m_systemRegistry.initializeAll(*this))
    {
        Logger::error("Failed to initialize domain systems");
        return false;
    }

    // Post-init wiring: give the controller terrain reference for walk-mode collision
    if (m_terrain && m_terrain->isInitialized())
    {
        m_controller->setTerrain(m_terrain);
    }

    // Apply --isolate-feature CLI override (visual-regression bisection).
    // Logged loudly so accompanying frame reports are easy to attribute.
    if (!config.isolateFeature.empty())
    {
        const std::string& f = config.isolateFeature;
        Logger::info("=== --isolate-feature=" + f + " ===");
        if (f == "motion-overlay")
        {
            m_renderer->setObjectMotionOverlayEnabled(false);
        }
        else if (f == "bloom")
        {
            m_renderer->setBloomEnabled(false);
        }
        else if (f == "ssao")
        {
            m_renderer->setSsaoEnabled(false);
        }
        else if (f == "ibl")
        {
            m_renderer->setIblMultiplierOverride(0.0f);
        }
        else if (f == "ibl-diffuse")
        {
            m_renderer->setIblSubScales(0.0f, 1.0f);
        }
        else if (f == "ibl-specular")
        {
            m_renderer->setIblSubScales(1.0f, 0.0f);
        }
        else if (f == "sh-grid")
        {
            m_renderer->setShGridForceDisabled(true);
        }
        else
        {
            Logger::warning("Unknown --isolate-feature value: '" + f +
                "' (expected: motion-overlay, bloom, ssao, ibl, "
                "ibl-diffuse, ibl-specular, sh-grid)");
        }
    }

    m_isRunning = true;
    Logger::info("Engine initialized successfully");
    Logger::info("Controls: Escape=toggle editor/play, WASD=move (play mode), Mouse=look (play mode), E=interact, F1=wireframe, F2=tonemapper, F3=HDR debug, F4=POM, F5=bloom, F6=SSAO, F7=AA mode (None/MSAA/TAA/SMAA), F8=color grading, F9=CSM debug, F10=auto-exposure, F11=diagnostic capture, V=frame cap cycle, P=toggle physics controller, G=walk/fly, Ctrl+Q=quit");
    Logger::info("Editor camera: Alt+LMB=orbit, MMB=pan, Scroll=zoom, F=focus, Numpad 1/3/7=front/right/top");
    Logger::info("Gamepad: Left stick=move, Right stick=look, LB=sprint, Triggers=up/down");
    return true;
}

void Engine::run()
{
    Logger::info("Entering main loop...");

    // Visual test mode: switch to play mode (full-screen blit) and disable FPC
    if (m_visualTestMode)
    {
        if (m_editor)
        {
            m_editor->setMode(EditorMode::PLAY);
            m_isCursorCaptured = true;
            m_window->setCursorEnabled(false);
        }
        m_controller->setEnabled(false);
        m_visualTestRunner.start("Testing/visual_tests");
    }

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
                m_waterFbo->resize(vpW / 4, vpH / 4, vpW / 4, vpH / 4);
            }
        }
        else
        {
            // Play mode — render at configured play resolution (independent of window)
            int rw = m_editor ? m_editor->getPlayModeWidth() : m_window->getWidth();
            int rh = m_editor ? m_editor->getPlayModeHeight() : m_window->getHeight();
            m_renderer->resizeRenderTarget(rw, rh);
            m_waterFbo->resize(rw / 4, rh / 4, rw / 4, rh / 4);
        }

        // 3e. Editor viewport interaction and camera update
        if (editorActive)
        {
            // Viewport click processing (uses previous frame's viewport bounds)
            m_editor->processViewportClick(
                m_renderer->getRenderWidth(), m_renderer->getRenderHeight());

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
                                       *m_foliageManager, m_editor->getCommandHistory());
                }
            }

            // 3g. Process terrain brush input for sculpting/painting
            TerrainBrush& terrainBrush = m_editor->getTerrainBrush();
            if (terrainBrush.isActive() && !m_editor->isGizmoActive()
                && m_terrainEnabled && m_terrain->isInitialized())
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
                                              *m_terrain, m_editor->getCommandHistory());
                }
            }

            // 3h. Process architectural tool clicks (wall, room, roof, stair, path)
            {
                Scene* toolScene = m_sceneManager->getActiveScene();
                int vpW = 0, vpH = 0;
                m_editor->getViewportSize(vpW, vpH);
                ImGuiIO& toolIo = ImGui::GetIO();
                bool clicked = toolIo.MouseClicked[0] && !toolIo.KeyAlt
                             && !m_editor->isGizmoActive();

                if (vpW > 0 && vpH > 0 && toolScene)
                {
                    float mouseX = (toolIo.MousePos.x - m_editor->getViewportMin().x)
                                 / static_cast<float>(vpW);
                    float mouseY = (toolIo.MousePos.y - m_editor->getViewportMin().y)
                                 / static_cast<float>(vpH);
                    float aspect = static_cast<float>(vpW) / static_cast<float>(vpH);
                    Ray mouseRay = BrushTool::createRay(*m_camera, mouseX, mouseY, aspect);

                    glm::vec3 hitPoint;
                    bool hasHit = BrushTool::rayGroundIntersect(mouseRay, hitPoint);

                    // Wall tool
                    WallTool& wallTool = m_editor->getWallTool();
                    if (wallTool.isActive())
                    {
                        if (hasHit)
                        {
                            wallTool.queueDebugDraw(hitPoint);
                        }
                        if (clicked && hasHit)
                        {
                            wallTool.processClick(hitPoint, *toolScene,
                                *m_resourceManager, m_editor->getCommandHistory());
                        }
                    }

                    // Room tool (click mode)
                    RoomTool& roomTool = m_editor->getRoomTool();
                    if (roomTool.getState() == RoomTool::State::WAITING_CORNER
                        && clicked && hasHit)
                    {
                        roomTool.processClick(hitPoint, *toolScene,
                            *m_resourceManager, m_editor->getCommandHistory());
                    }

                    // Cutout tool (select wall by picking)
                    CutoutTool& cutoutTool = m_editor->getCutoutTool();
                    if (cutoutTool.getState() == CutoutTool::State::SELECT_WALL
                        && clicked)
                    {
                        int pickX = static_cast<int>(mouseX * static_cast<float>(m_renderer->getRenderWidth()));
                        int pickY = static_cast<int>((1.0f - mouseY) * static_cast<float>(m_renderer->getRenderHeight()));
                        uint32_t entityId = m_renderer->pickEntityAt(pickX, pickY);
                        if (entityId != 0)
                        {
                            cutoutTool.selectWall(entityId, *toolScene);
                        }
                    }

                    // Roof tool (click to place)
                    RoofTool& roofTool = m_editor->getRoofTool();
                    if (roofTool.getState() == RoofTool::State::PLACING
                        && clicked && hasHit)
                    {
                        roofTool.processClick(hitPoint, *toolScene,
                            *m_resourceManager, m_editor->getCommandHistory());
                    }

                    // Stair tool (click to place)
                    StairTool& stairTool = m_editor->getStairTool();
                    if (stairTool.getState() == StairTool::State::PLACING
                        && clicked && hasHit)
                    {
                        stairTool.processClick(hitPoint, *toolScene,
                            *m_resourceManager, m_editor->getCommandHistory());
                    }

                    // Path tool
                    PathTool& pathTool = m_editor->getPathTool();
                    if (pathTool.isActive())
                    {
                        if (hasHit)
                        {
                            pathTool.queueDebugDraw(hitPoint);
                        }
                        if (clicked && hasHit)
                        {
                            pathTool.processClick(hitPoint);
                        }
                    }
                    if (pathTool.finishRequested())
                    {
                        pathTool.finishPath(*toolScene,
                            *m_resourceManager, m_editor->getCommandHistory());
                    }
                }
            }
        }

        // Start profiler early to capture CPU systems (cloth, physics) not just rendering
        m_profiler.beginFrame();

        // 3b. Environment — now handled by AtmosphereSystem via SystemRegistry::updateAll()

        // 4. Scene — update entities and components (includes cloth simulation)
        {
            VESTIGE_PROFILE_SCOPE("SceneUpdate");
            m_sceneManager->update(deltaTime);
        }

        // 4b. Physics — step the Jolt simulation
        if (m_physicsWorld.isInitialized())
        {
            VESTIGE_PROFILE_SCOPE("JoltPhysics");
            m_physicsWorld.update(deltaTime);
            m_physicsWorld.checkBreakableConstraints(deltaTime);
        }

        // 4c. Domain systems — update all active domain systems
        {
            VESTIGE_PROFILE_SCOPE("DomainSystems");
            m_systemRegistry.updateAll(deltaTime);
        }

        // 5. Controller — process input and update camera
        Scene* activeScene = m_sceneManager->getActiveScene();
        if (m_usePhysicsController && m_physicsCharController->isInitialized())
        {
            // Physics controller path: input -> velocity -> Jolt -> camera
            m_controller->processLookOnly(deltaTime);
            glm::vec3 velocity = m_controller->computeDesiredVelocity(deltaTime);
            m_physicsCharController->update(deltaTime, velocity);
            m_camera->setPosition(m_physicsCharController->getEyePosition());
        }
        else
        {
            // Legacy AABB controller path
            if (activeScene)
            {
                activeScene->collectColliders(m_colliders);
            }
            else
            {
                m_colliders.clear();
            }
            m_controller->update(deltaTime, m_colliders);
        }

        // 6. Renderer — draw the frame
        int winHeight = m_window->getHeight();
        if (winHeight <= 0)
        {
            m_window->pollEvents();
            continue;  // Skip rendering when minimized
        }

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

            // Set caustics params if water exists (used by scene + terrain shaders)
            if (!m_renderData.waterSurfaces.empty())
            {
                const auto& waterMatrix = m_renderData.waterSurfaces[0].second;
                float waterY = waterMatrix[3][1];
                float waterX = waterMatrix[3][0];
                float waterZ = waterMatrix[3][2];
                float elapsed = static_cast<float>(m_timer->getElapsedTime());

                // Get water surface dimensions for XZ bounds
                const auto& waterCfg = m_renderData.waterSurfaces[0].first->getConfig();
                glm::vec2 center(waterX, waterZ);
                glm::vec2 halfExtent(waterCfg.width * 0.5f, waterCfg.depth * 0.5f);

                // Quality tier: use per-surface config if set, else FormulaQualityManager
                int waterQuality = waterCfg.qualityTier;
                if (waterQuality == 0 && m_formulaQuality.hasCategoryOverride("water"))
                {
                    waterQuality = static_cast<int>(m_formulaQuality.getEffectiveTier("water"));
                }
                m_renderer->setCausticsQuality(waterQuality);
                m_terrainRenderer->setCausticsQuality(waterQuality);
                m_waterRenderer->setWaterQualityTier(waterQuality);

                bool causticsOn = waterCfg.causticsEnabled;
                m_renderer->setCausticsParams(causticsOn, waterY, elapsed,
                                               center, halfExtent,
                                               waterCfg.causticsIntensity,
                                               waterCfg.causticsScale);
                m_terrainRenderer->setCausticsParams(causticsOn, waterY, elapsed,
                                                     m_renderer->getCausticsTexture(),
                                                     center, halfExtent,
                                                     waterCfg.causticsIntensity,
                                                     waterCfg.causticsScale);
            }
            else
            {
                m_renderer->setCausticsParams(false, 0.0f, 0.0f);
                m_terrainRenderer->setCausticsParams(false, 0.0f, 0.0f, 0);
            }

            m_profiler.getGpuTimer().beginPass("Scene");
            {
                VESTIGE_PROFILE_SCOPE("RenderScene");
                m_renderer->renderScene(m_renderData, *m_camera, aspectRatio);
            }
            m_profiler.getGpuTimer().endPass();

            // Render terrain (after opaques, before foliage/water)
            if (m_terrainEnabled && m_terrain->isInitialized())
            {
                m_profiler.getGpuTimer().beginPass("Terrain");
                m_terrainRenderer->render(*m_terrain, *m_camera, aspectRatio, m_renderData,
                                         m_renderer->getCascadedShadowMap());
                m_profiler.getGpuTimer().endPass();
            }

            // Render foliage (after opaques, before water/particles)
            {
                glm::mat4 viewProj = m_camera->getProjectionMatrix(aspectRatio)
                                   * m_camera->getViewMatrix();
                // Out-param form reuses m_scratchVisibleChunks' capacity. (AUDIT H9.)
                m_foliageManager->getVisibleChunks(viewProj, m_scratchVisibleChunks);
                auto& visibleChunks = m_scratchVisibleChunks;
                float elapsed = static_cast<float>(m_timer->getElapsedTime());

                // Sync foliage wind with global environment
                m_foliageRenderer->windDirection = m_environmentForces->getBaseWindDirection();
                float envWindSpeed = m_environmentForces->getWindSpeed(m_camera->getPosition());
                m_foliageRenderer->windAmplitude = 0.08f * std::max(0.2f, envWindSpeed);

                if (!visibleChunks.empty())
                {
                    m_profiler.getGpuTimer().beginPass("Foliage");

                    // Pass shadow map and directional light to foliage for shadow receiving
                    CascadedShadowMap* csm = m_renderer->getCascadedShadowMap();
                    const DirectionalLight* dirLight =
                        m_renderData.hasDirectionalLight ? &m_renderData.directionalLight : nullptr;
                    m_foliageRenderer->render(visibleChunks, *m_camera, viewProj, elapsed,
                                             100.0f, csm, dirLight);
                    m_treeRenderer->render(visibleChunks, *m_camera, viewProj, elapsed);
                    m_profiler.getGpuTimer().endPass();
                }
            }

            // Render water surfaces (after opaques, before particles)
            if (!m_renderData.waterSurfaces.empty())
            {
                // Reuse vector across frames to avoid per-frame heap allocation
                static std::vector<WaterRenderItem> waterItems;
                waterItems.clear();
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

                // Save renderer view state (geometryOnly passes overwrite m_lastProjection etc.)
                m_renderer->saveViewState();

                // --- Refraction pass: render scene below water ---
                {
                    VESTIGE_PROFILE_SCOPE("WaterRefraction");
                    glm::vec4 refrClipPlane(0.0f, -1.0f, 0.0f, waterY + 0.1f);
                    glEnable(GL_CLIP_DISTANCE0);

                    m_waterFbo->bindRefraction();
                    m_renderer->renderScene(m_renderData, *m_camera, aspectRatio, refrClipPlane, true);

                    if (m_terrainEnabled && m_terrain->isInitialized())
                    {
                        m_terrainRenderer->render(*m_terrain, *m_camera, aspectRatio, m_renderData,
                                                 m_renderer->getCascadedShadowMap(), refrClipPlane);
                    }

                    glDisable(GL_CLIP_DISTANCE0);
                }

                // --- Reflection pass: render scene above water with reflected camera ---
                {
                    VESTIGE_PROFILE_SCOPE("WaterReflection");
                    glm::vec4 reflClipPlane(0.0f, 1.0f, 0.0f, -waterY + 0.1f);

                    // Create reflected camera: mirror position and pitch around water plane
                    Camera reflectedCamera = *m_camera;
                    glm::vec3 camPos = m_camera->getPosition();
                    float reflectedY = 2.0f * waterY - camPos.y;
                    reflectedCamera.setPosition(glm::vec3(camPos.x, reflectedY, camPos.z));
                    reflectedCamera.setPitch(-m_camera->getPitch());

                    glEnable(GL_CLIP_DISTANCE0);

                    m_waterFbo->bindReflection();
                    m_renderer->renderScene(m_renderData, reflectedCamera, aspectRatio, reflClipPlane, true);

                    if (m_terrainEnabled && m_terrain->isInitialized())
                    {
                        m_terrainRenderer->render(*m_terrain, reflectedCamera, aspectRatio, m_renderData,
                                                 m_renderer->getCascadedShadowMap(), reflClipPlane);
                    }

                    glDisable(GL_CLIP_DISTANCE0);
                }

                // Restore main scene FBO and view state after water passes
                m_renderer->rebindSceneFbo();
                m_renderer->restoreViewState();

                // Get reflection/refraction textures from water FBOs
                GLuint reflTex = m_waterFbo->getReflectionTexture();
                GLuint refrTex = m_waterFbo->getRefractionTexture();
                GLuint refrDepthTex = m_waterFbo->getRefractionDepthTexture();
                GLuint skyboxTex = m_renderer->getSkyboxTextureId();

                m_profiler.getGpuTimer().beginPass("Water");
                m_waterRenderer->render(waterItems, *m_camera, aspectRatio,
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
                m_particleRenderer->render(m_renderData.particleEmitters, *m_camera, viewProj,
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

            // Process pending box-select request
            int bx0, by0, bx1, by1;
            if (m_editor->consumeBoxSelect(bx0, by0, bx1, by1))
            {
                m_renderer->renderIdBuffer(m_renderData, *m_camera, aspectRatio);
                auto ids = m_renderer->pickEntitiesInRect(bx0, by0, bx1, by1);
                Selection& sel = m_editor->getSelection();
                sel.clearSelection();
                for (uint32_t id : ids)
                {
                    sel.addToSelection(id);
                }
                Logger::info("Box selected " + std::to_string(ids.size()) + " entities");
            }

            // Render selection outlines into the output FBO
            if (m_editor->getSelection().hasSelection())
            {
                m_renderer->renderSelectionOutline(
                    m_renderData, m_editor->getSelection().getSelectedIds(),
                    *m_camera, aspectRatio);
            }

            // 7c. Queue ground grid + light gizmos, then flush debug draw
            if (m_editor->isGridVisible())
            {
                // Ground-plane grid centered on camera XZ, snapped to 1m
                glm::vec3 camPos = m_camera->getPosition();
                float cx = std::floor(camPos.x);
                float cz = std::floor(camPos.z);
                const float halfExtent = 50.0f;  // 100m x 100m grid
                const glm::vec3 thinColor(0.15f, 0.15f, 0.15f);  // Dark neutral grey
                const glm::vec3 boldColor(0.30f, 0.30f, 0.30f);  // Brighter for 10m lines
                const float y = 0.03f;  // Well above ground to avoid z-fighting

                for (float x = cx - halfExtent; x <= cx + halfExtent; x += 1.0f)
                {
                    bool isBold = (static_cast<int>(std::round(x)) % 10 == 0);
                    const glm::vec3& color = isBold ? boldColor : thinColor;
                    DebugDraw::line(
                        glm::vec3(x, y, cz - halfExtent),
                        glm::vec3(x, y, cz + halfExtent), color);
                }
                for (float z = cz - halfExtent; z <= cz + halfExtent; z += 1.0f)
                {
                    bool isBold = (static_cast<int>(std::round(z)) % 10 == 0);
                    const glm::vec3& color = isBold ? boldColor : thinColor;
                    DebugDraw::line(
                        glm::vec3(cx - halfExtent, y, z),
                        glm::vec3(cx + halfExtent, y, z), color);
                }
            }
            if (activeScene)
            {
                drawLightGizmos(*activeScene, m_editor->getSelection(),
                                m_editor->isShowAllLightGizmos());
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
            m_editor->drawPanels(m_renderer.get(), activeScene, m_camera.get(),
                                 m_timer.get(), m_window.get());

            // Handle menu-triggered screenshot (same as F11)
            if (m_editor->consumeScreenshotRequest())
            {
                FrameDiagnostics::capture(*m_renderer, *m_camera,
                    m_window->getWidth(), m_window->getHeight(),
                    m_timer->getFps(), m_timer->getDeltaTime());
            }

            m_editor->endFrame();
        }
        else
        {
            // Play mode: blit render FBO to screen (may scale if resolutions differ)
            m_renderer->blitToScreen(m_window->getWidth(), m_window->getHeight());

            // Render in-game UI overlay (after scene, before editor)
            if (m_uiSystem && m_uiSystem->isActive())
            {
                m_uiSystem->renderUI(m_window->getWidth(), m_window->getHeight());
            }

            if (m_editor)
            {
                m_editor->drawPanels(nullptr, nullptr, nullptr,
                                     m_timer.get(), m_window.get());
                m_editor->endFrame();
            }
        }

        // 8.5. Visual test runner — capture screenshots at predefined viewpoints
        if (m_visualTestMode)
        {
            if (m_visualTestRunner.update(*m_camera, *m_renderer,
                                           m_window->getWidth(), m_window->getHeight(),
                                           m_timer->getFps(), m_timer->getDeltaTime()))
            {
                m_isRunning = false;
                break;
            }
        }

        // 9. Window — swap buffers (flushes GPU work, making query results available)
        m_window->swapBuffers();

        // 10. Frame rate cap (busy-wait for target frame time if capped)
        m_timer->waitForFrameCap();

        // 11. End profiler frame (collect GPU results after swap ensures queries are complete)
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

    // Shut down domain systems (handles terrain, foliage, water, particles, character controller)
    // Must happen before destroying the window (GL context).
    //
    // AUDIT.md §H17: shutdownAll() invokes each system's shutdown() but does
    // NOT destroy the system instances — the unique_ptrs remain in the
    // registry. Without a follow-up clear(), the systems' destructors only
    // run during ~Engine member cleanup, AFTER m_renderer.reset() and
    // m_window.reset() below have freed the renderer + GL context.
    // clear() forces destruction here while those dependencies are alive.
    m_systemRegistry.shutdownAll();
    m_systemRegistry.clear();

    // Shut down remaining engine-owned subsystems
    m_profiler.shutdown();
    m_physicsWorld.shutdown();
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
    Logger::closeLogFile();
}

void Engine::createPhysicsStaticBodies()
{
    if (!m_physicsWorld.isInitialized())
    {
        return;
    }

    Scene* scene = m_sceneManager->getActiveScene();
    if (!scene)
    {
        return;
    }

    // Convert all scene AABB colliders to static Jolt box bodies
    std::vector<AABB> colliders;
    scene->collectColliders(colliders);

    int count = 0;
    for (const auto& aabb : colliders)
    {
        glm::vec3 center = aabb.getCenter();
        glm::vec3 size = aabb.getSize();
        glm::vec3 halfExtents = size * 0.5f;

        // Skip degenerate AABBs — Jolt's default convex radius is 0.05,
        // so half-extents must be at least that large
        constexpr float MIN_HALF_EXTENT = 0.05f;
        if (halfExtents.x < MIN_HALF_EXTENT || halfExtents.y < MIN_HALF_EXTENT ||
            halfExtents.z < MIN_HALF_EXTENT)
        {
            continue;
        }

        JPH::BoxShape* shape = new JPH::BoxShape(
            JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
        m_physicsWorld.createStaticBody(shape, center);
        ++count;
    }

    // Add a large ground plane if no terrain-sized collider exists
    JPH::BoxShape* ground = new JPH::BoxShape(JPH::Vec3(500, 0.5f, 500));
    m_physicsWorld.createStaticBody(ground, glm::vec3(0, -0.5f, 0));

    Logger::info("Physics: created " + std::to_string(count) +
                 " static bodies from scene + ground plane");
}

void Engine::setupVisualTestViewpoints()
{
    // Tabernacle scene coordinates (must match setupTabernacleScene())
    const float C = 0.445f;
    const float tentL = 30.0f * C;              // 13.35m
    const float frontZ = tentL;                  // Tent entrance
    const float veilZ = 10.0f * C;              // 4.45m (Holy of Holies divider)
    const float courtMargin = 5.0f * C;
    const float courtL = 100.0f * C;            // 44.50m
    const float courtWestZ = 0.0f - courtMargin; // -2.225m
    const float courtEastZ = courtWestZ + courtL; // 42.275m
    const float altarZ = courtEastZ - 10.0f * C; // 37.825m

    const float eyeHeight = 1.7f;

    // 1. Outside the eastern gate — full panorama of the desert + courtyard entrance
    m_visualTestRunner.addViewpoint({"gate_exterior",
        glm::vec3(0.0f, 2.0f, courtEastZ + 5.0f), -90.0f, -5.0f, 8, 45.0f});

    // 2. Just inside the gate — looking at the Bronze Altar
    m_visualTestRunner.addViewpoint({"gate_interior",
        glm::vec3(0.0f, eyeHeight, courtEastZ - 2.0f), -90.0f, 0.0f, 8, 45.0f});

    // 3. Near the Bronze Altar — slightly offset to see it
    m_visualTestRunner.addViewpoint({"bronze_altar",
        glm::vec3(2.0f, eyeHeight, altarZ), -135.0f, -5.0f, 8, 45.0f});

    // 4. Courtyard centre — midway between altar and tent
    m_visualTestRunner.addViewpoint({"courtyard_centre",
        glm::vec3(0.0f, eyeHeight, (altarZ + frontZ) / 2.0f), -90.0f, 0.0f, 8, 45.0f});

    // 5. At the tent entrance — looking into the Holy Place
    m_visualTestRunner.addViewpoint({"tent_entrance",
        glm::vec3(0.0f, eyeHeight, frontZ - 0.5f), -90.0f, 0.0f, 8, 45.0f});

    // 6. Centre of the Holy Place — see Menorah, Table, Incense Altar
    m_visualTestRunner.addViewpoint({"holy_place",
        glm::vec3(0.0f, eyeHeight, (frontZ + veilZ) / 2.0f), -90.0f, 0.0f, 8, 45.0f});

    // 7. Near the Menorah (south wall) — looking across
    m_visualTestRunner.addViewpoint({"near_menorah",
        glm::vec3(-1.5f, eyeHeight, 9.0f), 0.0f, 0.0f, 8, 45.0f});

    // 8. Before the Veil — looking toward the Holy of Holies
    m_visualTestRunner.addViewpoint({"before_veil",
        glm::vec3(0.0f, eyeHeight, veilZ + 1.5f), -90.0f, 0.0f, 8, 45.0f});
}

void Engine::drawLightGizmos(Scene& scene, const Selection& selection,
                             bool showAll)
{
    // Helper lambda to draw gizmos for a single entity
    auto drawGizmosForEntity = [&](Entity* entity, float brightness)
    {
        glm::vec3 worldPos = entity->getWorldPosition();

        // Directional light: 3 parallel arrows showing direction
        if (auto* dirComp = entity->getComponent<DirectionalLightComponent>())
        {
            glm::vec3 color = dirComp->light.diffuse * brightness;
            glm::vec3 dir = glm::normalize(dirComp->light.direction);
            float arrowLen = 2.0f;
            float spacing = 0.3f;

            glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                           ? glm::vec3(1.0f, 0.0f, 0.0f)
                           : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::normalize(glm::cross(dir, up));
            glm::vec3 fwd = glm::cross(right, dir);

            DebugDraw::arrow(worldPos, worldPos + dir * arrowLen, color);
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
            glm::vec3 color = ptComp->light.diffuse * brightness;
            float range = calculateLightRange(ptComp->light.constant,
                                              ptComp->light.linear,
                                              ptComp->light.quadratic);
            range = std::min(range, 200.0f);
            DebugDraw::wireSphere(worldPos, range, color);
        }

        // Spot light: cone wireframe
        if (auto* spotComp = entity->getComponent<SpotLightComponent>())
        {
            glm::vec3 color = spotComp->light.diffuse * brightness;
            float range = calculateLightRange(spotComp->light.constant,
                                              spotComp->light.linear,
                                              spotComp->light.quadratic);
            range = std::min(range, 200.0f);

            float outerAngleDeg = glm::degrees(
                std::acos(std::clamp(spotComp->light.outerCutoff, -1.0f, 1.0f)));
            DebugDraw::cone(worldPos, spotComp->light.direction,
                            range, outerAngleDeg, color);

            float innerAngleDeg = glm::degrees(
                std::acos(std::clamp(spotComp->light.innerCutoff, -1.0f, 1.0f)));
            glm::vec3 dimColor = color * 0.4f;
            DebugDraw::cone(worldPos, spotComp->light.direction,
                            range, innerAngleDeg, dimColor, 4);
        }
    };

    // Draw dimmed gizmos for all lights when showAll is enabled
    if (showAll)
    {
        const auto& selectedIds = selection.getSelectedIds();
        scene.forEachEntity([&](Entity& entity)
        {
            bool isSelected = std::find(selectedIds.begin(), selectedIds.end(),
                                        entity.getId()) != selectedIds.end();
            if (isSelected)
            {
                return;
            }

            bool hasLight = entity.hasComponent<DirectionalLightComponent>()
                         || entity.hasComponent<PointLightComponent>()
                         || entity.hasComponent<SpotLightComponent>();
            if (hasLight)
            {
                drawGizmosForEntity(&entity, 0.3f);
            }
        });
    }

    // Draw full-brightness gizmos for selected lights
    if (selection.hasSelection())
    {
        for (uint32_t id : selection.getSelectedIds())
        {
            Entity* entity = scene.findEntityById(id);
            if (entity)
            {
                drawGizmosForEntity(entity, 1.0f);
            }
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
    // Untextured grey ground — the original everytexture.com rock
    // textures are not redistributable in the public repo. A richer
    // ground material will land via the separate VestigeAssets repo
    // (see ASSET_LICENSES.md). Local maintainers retain the original
    // files (gitignored) and can re-enable a textured ground there.
    groundMat->setAlbedo(glm::vec3(0.55f, 0.52f, 0.50f));
    groundMat->setMetallic(0.0f);
    groundMat->setRoughness(0.9f);

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
    // The original Texturelabs metal diffuse is not redistributable; the
    // PBR metallic/roughness/albedo combination below renders convincingly
    // as gold on its own. A richer textured gold is queued for the
    // separate VestigeAssets repo (see ASSET_LICENSES.md).
    auto goldMat = m_resourceManager->createMaterial("gold");
    goldMat->setType(MaterialType::PBR);
    goldMat->setAlbedo(glm::vec3(1.4f, 1.1f, 0.5f));
    goldMat->setMetallic(1.0f);
    goldMat->setRoughness(0.25f);

    // Block 3 — Wood (right)
    // Switched from non-redistributable Texturelabs_Glass_120M to the
    // CC0 Poly Haven `plank_flooring_04` set already shipped in the repo.
    // Adds proper normal + roughness maps that the previous setup lacked.
    auto woodMat = m_resourceManager->createMaterial("wood");
    woodMat->setType(MaterialType::PBR);
    woodMat->setAlbedo(glm::vec3(1.0f));
    woodMat->setMetallic(0.0f);
    woodMat->setRoughness(0.9f);
    woodMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/plank_flooring_04_diff_2k.jpg"));
    woodMat->setNormalMap(m_resourceManager->loadTexture("assets/textures/plank_flooring_04_nor_gl_2k.jpg", true));
    woodMat->setMetallicRoughnessTexture(
        m_resourceManager->loadTexture("assets/textures/plank_flooring_04_rough_2k.jpg", true));

    // Block 4 — Rough Brick (back)
    auto roughBrickMat = m_resourceManager->createMaterial("rough_brick");
    roughBrickMat->setType(MaterialType::PBR);
    roughBrickMat->setAlbedo(glm::vec3(1.0f));
    roughBrickMat->setMetallic(0.0f);
    roughBrickMat->setRoughness(0.9f);
    roughBrickMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/brick_wall_005_diff_2k.jpg"));
    roughBrickMat->setNormalMap(m_resourceManager->loadTexture("assets/textures/brick_wall_005_nor_gl_2k.jpg", true));

    // --- Height maps for POM ---

    // Ground height map removed alongside the everytexture rock textures
    // (see notes above). When the VestigeAssets repo ships a ground
    // material it can rewire setHeightMap() here.

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

    // --- Animated glTF model (skeletal animation test) ---
    auto cesiumMan = m_resourceManager->loadModel("assets/models/CesiumMan.glb");
    if (cesiumMan)
    {
        Entity* animRoot = cesiumMan->instantiate(*scene, nullptr, "CesiumMan");
        animRoot->transform.position = glm::vec3(-3.0f, 0.0f, 4.0f);
        animRoot->transform.scale = glm::vec3(2.0f);
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
        wCfg.gridResolution = 128;
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
                m_foliageManager->paintFoliage(
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
            m_foliageManager->eraseAllFoliage(ex.pos, ex.radius);
        }

        Logger::info("Demo foliage: " + std::to_string(m_foliageManager->getTotalFoliageCount())
                     + " grass instances across " + std::to_string(m_foliageManager->getChunkCount()) + " chunks");
    }

    // Initial scene update to compute world matrices
    scene->update(0.0f);

    Logger::info("Demo scene ready: entities with components, 1 directional + 2 point lights + emissive lava + glass cube + foliage");
}

void Engine::setupTabernacleScene()
{
    Logger::info("Setting up Tabernacle scene...");

    // =========================================================================
    // Biblical Tabernacle / Tent of Meeting (Exodus 25-40)
    //
    // Dimensions (1 cubit = 0.445m, standard/common cubit):
    //   Tabernacle tent: 30 x 10 x 10 cubits = 13.35 x 4.45 x 4.45 m
    //   Holy Place:      20 x 10 x 10 cubits = 8.90 x 4.45 x 4.45 m
    //   Holy of Holies:  10 x 10 x 10 cubits = 4.45 x 4.45 x 4.45 m (perfect cube)
    //   Outer Court:     100 x 50 cubits     = 44.50 x 22.25 m
    //
    // Layout: Entrance faces East. Holy of Holies is at the West end.
    //   East -> entrance -> Holy Place -> veil -> Holy of Holies -> West
    //   Z-axis = East-West (entrance at +Z, back at -Z)
    //   X-axis = North-South
    // =========================================================================

    // --- Renderer configuration: outdoor desert scene ---
    m_terrainEnabled = false;
    m_renderer->setSkyboxEnabled(true);
    m_renderer->setClearColor(glm::vec3(0.53f, 0.68f, 0.86f));
    m_renderer->setAutoExposure(false);
    m_renderer->setExposure(3.0f);  // High: compensates for bright HDRI sky in tonemapper
    m_renderer->setBloomEnabled(true);
    m_renderer->setBloomThreshold(2.5f);   // High threshold: only bright specular highlights bloom
    m_renderer->setBloomIntensity(0.10f);  // Very subtle
    m_renderer->setSsaoEnabled(true);

    // Load desert HDRI skybox (Goegap Nature Reserve, South Africa — arid Namaqualand).
    // The `tabernacle/` asset directory is part of the separate biblical-project
    // repo (commercial Steam release) and is not present in fresh public clones
    // of the engine. Local maintainers retain it for development; the call below
    // will silently fail to load when the file is absent, leaving the previous
    // skybox state in place.
    m_renderer->loadSkyboxHDRI("assets/textures/tabernacle/goegap_2k.hdr");

    Scene* scene = m_sceneManager->createScene("Tabernacle");

    // --- Cubit conversion ---
    const float C = 0.445f;  // 1 cubit in meters (standard/common cubit)

    // --- Tent dimensions ---
    const float tentL = 30.0f * C;   // 13.35m length (Z)
    const float tentW = 10.0f * C;   // 4.45m width (X)
    const float tentH = 10.0f * C;   // 4.45m height (Y)
    const float wallThick = 0.40f;   // Wall/board thickness (thick enough for CSM shadow coverage)

    // Holy Place / Holy of Holies split
    const float holyPlaceL = 20.0f * C;    // 8.90m
    const float holyOfHoliesL = 10.0f * C; // 4.45m

    // Tent placement: back wall at Z=0, entrance at Z=tentL
    const float backZ = 0.0f;
    const float frontZ = tentL;
    const float veilZ = holyOfHoliesL;  // Divider between HoH and HP

    // --- Courtyard dimensions ---
    const float courtL = 100.0f * C;   // 44.50m (Z-axis)
    const float courtW = 50.0f * C;    // 22.25m (X-axis)
    const float fenceH = 5.0f * C;     // 2.225m curtain wall height
    const float courtMargin = 5.0f * C; // Space behind tent
    const float courtWestZ = backZ - courtMargin;
    const float courtEastZ = courtWestZ + courtL;
    const float courtCenterZ = (courtWestZ + courtEastZ) / 2.0f;
    const float gateW = 20.0f * C;       // 8.90m gate opening
    const float pillarSpacing = 5.0f * C; // 2.225m between courtyard pillars

    // --- Mesh primitives ---
    auto cubeMesh   = m_resourceManager->getCubeMesh();
    auto cylMesh    = m_resourceManager->getCylinderMesh(16);
    auto sphereMesh = m_resourceManager->getSphereMesh(16, 8);

    // --- Texture paths ---
    const std::string texBase = "assets/textures/tabernacle/";

    // =====================================================================
    // MATERIALS
    // =====================================================================

    // Acacia wood (boards, pillars, furniture frames)
    auto woodMat = m_resourceManager->createMaterial("tab_acacia_wood");
    woodMat->setType(MaterialType::PBR);
    woodMat->setAlbedo(glm::vec3(1.0f));
    woodMat->setMetallic(0.0f);
    woodMat->setRoughness(0.98f);   // Unvarnished ancient acacia — very rough, no sheen
    woodMat->setDiffuseTexture(m_resourceManager->loadTexture(texBase + "brown_planks_09_diff.jpg"));
    woodMat->setNormalMap(m_resourceManager->loadTexture(texBase + "brown_planks_09_nor_gl.jpg", true));
    woodMat->setMetallicRoughnessTexture(m_resourceManager->loadTexture(texBase + "brown_planks_09_rough.jpg", true));
    woodMat->setDoubleSided(true);

    // Gold (overlays on Ark, Menorah, Incense Altar, Table)
    auto goldMat = m_resourceManager->createMaterial("tab_gold");
    goldMat->setType(MaterialType::PBR);
    goldMat->setAlbedo(glm::vec3(1.0f, 0.77f, 0.31f));  // Physically-based gold
    goldMat->setMetallic(1.0f);
    goldMat->setRoughness(0.50f);  // Beaten/hammered gold -- higher roughness shows normal map detail
    goldMat->setNormalMap(m_resourceManager->loadTexture(texBase + "hammered_gold_nor_gl.jpg", true));
    goldMat->setDoubleSided(true);

    // Bronze (altar, laver, pillar bases)
    auto bronzeMat = m_resourceManager->createMaterial("tab_bronze");
    bronzeMat->setType(MaterialType::PBR);
    bronzeMat->setAlbedo(glm::vec3(0.90f, 0.65f, 0.45f));  // Physically-based bronze
    bronzeMat->setMetallic(1.0f);
    bronzeMat->setRoughness(0.40f);
    bronzeMat->setNormalMap(m_resourceManager->loadTexture(texBase + "bronze_nor_gl.jpg", true));
    bronzeMat->setDoubleSided(true);

    // Silver (courtyard pillar caps, board sockets)
    auto silverMat = m_resourceManager->createMaterial("tab_silver");
    silverMat->setType(MaterialType::PBR);
    silverMat->setAlbedo(glm::vec3(0.95f, 0.93f, 0.88f));
    silverMat->setMetallic(1.0f);
    silverMat->setRoughness(0.30f);
    silverMat->setNormalMap(m_resourceManager->loadTexture(texBase + "silver_nor_gl.jpg", true));
    silverMat->setDoubleSided(true);

    // Linen curtains (inner tent curtains, slightly off-white)
    auto linenMat = m_resourceManager->createMaterial("tab_linen");
    linenMat->setType(MaterialType::PBR);
    linenMat->setAlbedo(glm::vec3(0.9f, 0.88f, 0.82f));
    linenMat->setMetallic(0.0f);
    linenMat->setRoughness(0.9f);
    linenMat->setDiffuseTexture(m_resourceManager->loadTexture(texBase + "rough_linen_diff.jpg"));
    linenMat->setNormalMap(m_resourceManager->loadTexture(texBase + "rough_linen_nor_gl.jpg", true));
    linenMat->setMetallicRoughnessTexture(m_resourceManager->loadTexture(texBase + "rough_linen_rough.jpg", true));
    linenMat->setUvScale(2.0f);
    linenMat->setDoubleSided(true);

    // White linen (courtyard hangings -- bright white)
    auto whiteLinenMat = m_resourceManager->createMaterial("tab_white_linen");
    whiteLinenMat->setType(MaterialType::PBR);
    whiteLinenMat->setAlbedo(glm::vec3(0.95f, 0.93f, 0.90f));
    whiteLinenMat->setMetallic(0.0f);
    whiteLinenMat->setRoughness(0.9f);
    whiteLinenMat->setDiffuseTexture(m_resourceManager->loadTexture(texBase + "rough_linen_diff.jpg"));
    whiteLinenMat->setNormalMap(m_resourceManager->loadTexture(texBase + "rough_linen_nor_gl.jpg", true));
    whiteLinenMat->setMetallicRoughnessTexture(m_resourceManager->loadTexture(texBase + "rough_linen_rough.jpg", true));
    whiteLinenMat->setUvScale(2.0f);
    whiteLinenMat->setDoubleSided(true);

    // Goat hair covering (dark, coarse)
    auto goatHairMat = m_resourceManager->createMaterial("tab_goat_hair");
    goatHairMat->setType(MaterialType::PBR);
    goatHairMat->setAlbedo(glm::vec3(0.25f, 0.22f, 0.18f));
    goatHairMat->setMetallic(0.0f);
    goatHairMat->setRoughness(0.95f);
    goatHairMat->setDiffuseTexture(m_resourceManager->loadTexture(texBase + "hessian_230_diff.jpg"));
    goatHairMat->setNormalMap(m_resourceManager->loadTexture(texBase + "hessian_230_nor_gl.jpg", true));
    goatHairMat->setMetallicRoughnessTexture(m_resourceManager->loadTexture(texBase + "hessian_230_rough.jpg", true));
    goatHairMat->setUvScale(3.0f);
    goatHairMat->setDoubleSided(true);

    // Ram skins dyed red (3rd covering layer)
    auto redLeatherMat = m_resourceManager->createMaterial("tab_red_leather");
    redLeatherMat->setType(MaterialType::PBR);
    redLeatherMat->setAlbedo(glm::vec3(0.7f, 0.15f, 0.1f));
    redLeatherMat->setMetallic(0.0f);
    redLeatherMat->setRoughness(0.75f);
    redLeatherMat->setDiffuseTexture(m_resourceManager->loadTexture(texBase + "red_leather_diff.jpg"));
    redLeatherMat->setNormalMap(m_resourceManager->loadTexture(texBase + "red_leather_nor_gl.jpg", true));
    redLeatherMat->setMetallicRoughnessTexture(m_resourceManager->loadTexture(texBase + "red_leather_rough.jpg", true));
    redLeatherMat->setUvScale(2.0f);
    redLeatherMat->setDoubleSided(true);

    // Tachash skins (4th/outermost covering layer)
    auto darkHideMat = m_resourceManager->createMaterial("tab_dark_hide");
    darkHideMat->setType(MaterialType::PBR);
    darkHideMat->setAlbedo(glm::vec3(0.25f, 0.22f, 0.20f));
    darkHideMat->setMetallic(0.0f);
    darkHideMat->setRoughness(0.9f);
    darkHideMat->setDiffuseTexture(m_resourceManager->loadTexture(texBase + "dark_hide_diff.jpg"));
    darkHideMat->setNormalMap(m_resourceManager->loadTexture(texBase + "dark_hide_nor_gl.jpg", true));
    darkHideMat->setMetallicRoughnessTexture(m_resourceManager->loadTexture(texBase + "dark_hide_rough.jpg", true));
    darkHideMat->setUvScale(2.0f);
    darkHideMat->setDoubleSided(true);

    // Sandy ground
    auto sandMat = m_resourceManager->createMaterial("tab_sand");
    sandMat->setType(MaterialType::PBR);
    sandMat->setAlbedo(glm::vec3(1.0f));
    sandMat->setMetallic(0.0f);
    sandMat->setRoughness(1.0f);  // Fully rough -- no specular hotspot on desert sand
    sandMat->setDiffuseTexture(m_resourceManager->loadTexture(texBase + "sandy_gravel_02_diff.jpg"));
    sandMat->setNormalMap(m_resourceManager->loadTexture(texBase + "sandy_gravel_02_nor_gl.jpg", true));
    // No metallicRoughness texture -- the rough.jpg has low-roughness areas that create
    // a camera-tracking specular highlight on the large flat ground plane
    sandMat->setStochasticTiling(true);
    sandMat->setUvScale(4.0f);
    sandMat->setDoubleSided(true);

    // Veil (blue/purple/scarlet -- the dividing curtain and courtyard gate)
    auto veilMat = m_resourceManager->createMaterial("tab_veil");
    veilMat->setType(MaterialType::PBR);
    veilMat->setAlbedo(glm::vec3(0.25f, 0.12f, 0.35f));
    veilMat->setMetallic(0.0f);
    veilMat->setRoughness(0.85f);
    veilMat->setDiffuseTexture(m_resourceManager->loadTexture(texBase + "rough_linen_diff.jpg"));
    veilMat->setNormalMap(m_resourceManager->loadTexture(texBase + "rough_linen_nor_gl.jpg", true));
    veilMat->setUvScale(1.5f);
    veilMat->setDoubleSided(true);
    // Subtle emissive glow: approximates subsurface scattering — sunlight
    // filtering through dense dyed fabric produces a warm reddish-purple glow
    // on the interior side. True SSS is planned for Phase 9.
    veilMat->setEmissive(glm::vec3(0.12f, 0.04f, 0.10f));
    veilMat->setEmissiveStrength(0.4f);
    // IBL multipliers left at default 1.0 — the light probe system handles
    // indoor/outdoor IBL transitions properly. Interior surfaces inside the
    // tent probe volume blend from sky IBL to captured interior IBL.

    // =====================================================================
    // HELPER LAMBDAS
    // =====================================================================

    auto makeBox = [&](const std::string& name, glm::vec3 pos, glm::vec3 size,
                       std::shared_ptr<Material> mat) -> Entity*
    {
        Entity* e = scene->createEntity(name);
        e->transform.position = pos;
        e->transform.scale = size;
        auto* mr = e->addComponent<MeshRenderer>(cubeMesh, mat);
        mr->setBounds(cubeMesh->getLocalBounds());
        return e;
    };

    // Cylinder: base mesh is 1m diameter, 1m tall, centered at origin
    auto makeCylinder = [&](const std::string& name, glm::vec3 pos, float diameter,
                            float height, std::shared_ptr<Material> mat) -> Entity*
    {
        Entity* e = scene->createEntity(name);
        e->transform.position = pos;
        e->transform.scale = glm::vec3(diameter, height, diameter);
        auto* mr = e->addComponent<MeshRenderer>(cylMesh, mat);
        mr->setBounds(cylMesh->getLocalBounds());
        return e;
    };

    // Sphere: base mesh is 1m diameter, centered at origin
    auto makeSphere = [&](const std::string& name, glm::vec3 pos, glm::vec3 scale,
                          std::shared_ptr<Material> mat) -> Entity*
    {
        Entity* e = scene->createEntity(name);
        e->transform.position = pos;
        e->transform.scale = scale;
        auto* mr = e->addComponent<MeshRenderer>(sphereMesh, mat);
        mr->setBounds(sphereMesh->getLocalBounds());
        return e;
    };

    // =====================================================================
    // GROUND -- desert sand covering courtyard and surroundings
    // =====================================================================
    auto desertPlane = m_resourceManager->getPlaneMesh(courtL + 10.0f);
    Entity* ground = scene->createEntity("Desert Ground");
    ground->transform.position = glm::vec3(0.0f, -0.02f, courtCenterZ);
    auto* groundMR = ground->addComponent<MeshRenderer>(desertPlane, sandMat);
    groundMR->setCastsShadow(false);

    // =====================================================================
    // TENT STRUCTURE -- Acacia wood boards forming walls
    // =====================================================================

    // South wall (X = -tentW/2)
    makeBox("South Wall", {-tentW / 2.0f, tentH / 2.0f, tentL / 2.0f},
            {wallThick, tentH, tentL}, woodMat);

    // North wall (X = +tentW/2)
    makeBox("North Wall", {tentW / 2.0f, tentH / 2.0f, tentL / 2.0f},
            {wallThick, tentH, tentL}, woodMat);

    // Back (West) wall (Z = 0)
    makeBox("West Wall", {0.0f, tentH / 2.0f, backZ},
            {tentW, tentH, wallThick}, woodMat);

    // =====================================================================
    // ROOF -- Four biblical covering layers (Exodus 26:1-14)
    // =====================================================================

    // Layer 1: Inner linen curtains with cherubim (visible from inside)
    // Roof layers are 0.15m thick each for proper CSM shadow coverage.
    // 0.20m center-to-center spacing gives 0.05m gaps (prevents z-fighting).
    const float roofThick = 0.15f;
    const float roofGap = 0.20f;
    makeBox("Linen Ceiling", {0.0f, tentH, tentL / 2.0f},
            {tentW + 0.3f, roofThick, tentL + 0.3f}, linenMat);

    // Layer 2: Goat hair curtains
    makeBox("Goat Hair Roof", {0.0f, tentH + roofGap, tentL / 2.0f},
            {tentW + 0.6f, roofThick, tentL + 0.6f}, goatHairMat);

    // Layer 3: Ram skins dyed red
    makeBox("Ram Skin Roof", {0.0f, tentH + roofGap * 2.0f, tentL / 2.0f},
            {tentW + 0.9f, roofThick, tentL + 0.9f}, redLeatherMat);

    // Layer 4: Tachash skins (outermost, largest)
    makeBox("Tachash Roof", {0.0f, tentH + roofGap * 3.0f, tentL / 2.0f},
            {tentW + 1.2f, roofThick, tentL + 1.2f}, darkHideMat);

    // Scene-wide prevailing wind — configure EnvironmentForces so all
    // consumers (cloth, foliage, particles, water) blow consistently.
    // Desert wind from the east (positive Z toward entrance, slight northward drift).
    m_environmentForces->setWindDirection(glm::vec3(0.15f, 0.0f, -1.0f));
    m_environmentForces->setWindStrength(1.0f);
    const glm::vec3 sceneWindDir = m_environmentForces->getBaseWindDirection();

    // Interior pillar dimensions (used by veil and curtain colliders)
    const float intPillarH = tentH - 0.3f;
    const float intPillarDiam = 0.24f;
    const int intPillarCount = 6;

    // Cloth hang point: below the linen ceiling bottom (tentH - roofThick/2)
    // with a small clearance gap to avoid spawning inside the ceiling.
    const float clothCeilingGap = 0.025f;  // 2.5cm clearance
    const float clothHangY = tentH - roofThick / 2.0f - clothCeilingGap;

    // =====================================================================
    // VEIL -- Animated cloth dividing curtain between Holy Place and Holy of Holies
    // Uses heavy drape preset — dense fabric that barely moves in the sheltered
    // interior. Emissive glow approximates subsurface scattering (true SSS
    // planned for Phase 9).
    // =====================================================================
    {
        ClothPresetConfig veilPreset = ClothPresets::heavyDrape();

        float veilW = tentW - 2.0f * wallThick - 0.05f;  // Fit within wall inner surfaces
        float veilH = tentH - 0.1f;
        float veilSpacing = 0.2f;
        uint32_t veilGridW = std::max(3u, static_cast<uint32_t>(veilW / veilSpacing) + 1);
        uint32_t veilGridH = std::max(3u, static_cast<uint32_t>(veilH / veilSpacing) + 1);
        veilSpacing = std::min(veilW / static_cast<float>(veilGridW - 1),
                               veilH / static_cast<float>(veilGridH - 1));

        ClothConfig veilCfg = veilPreset.solver;
        veilCfg.width = veilGridW;
        veilCfg.height = veilGridH;
        veilCfg.spacing = veilSpacing;

        Entity* veilEntity = scene->createEntity("Veil");
        veilEntity->transform.position = glm::vec3(0.0f);

        auto* veilCloth = veilEntity->addComponent<ClothComponent>();
        veilCloth->initialize(veilCfg, veilMat, 33333u);
        veilCloth->setPresetType(ClothPresetType::HEAVY_DRAPE);

        auto& veilSim = veilCloth->getSimulator();

        // Pin all at desired world positions, then unpin non-top-row
        for (uint32_t gz = 0; gz < veilGridH; ++gz)
        {
            for (uint32_t gx = 0; gx < veilGridW; ++gx)
            {
                uint32_t idx = gz * veilGridW + gx;
                float worldX = (static_cast<float>(gx) / static_cast<float>(veilGridW - 1) - 0.5f) * veilW;
                float worldY = clothHangY - static_cast<float>(gz) * veilSpacing;
                float worldZ = veilZ;
                veilSim.pinParticle(idx, glm::vec3(worldX, worldY, worldZ));
            }
        }
        for (uint32_t gz = 1; gz < veilGridH; ++gz)
        {
            for (uint32_t gx = 0; gx < veilGridW; ++gx)
            {
                veilSim.unpinParticle(gz * veilGridW + gx);
            }
        }

        // Interior: deep inside tent, heavily sheltered by walls and roof.
        // Only trace amounts of wind reach the veil — barely perceptible sway.
        veilSim.setWind(sceneWindDir, veilPreset.windStrength * 0.15f);
        veilSim.setDragCoefficient(veilPreset.dragCoefficient);
        veilSim.setGroundPlane(0.0f);

        // Keep veil within tent walls
        float wallInnerV = tentW / 2.0f - wallThick / 2.0f;
        veilSim.addPlaneCollider(glm::vec3(1, 0, 0), -wallInnerV);
        veilSim.addPlaneCollider(glm::vec3(-1, 0, 0), -wallInnerV);

        // Cylinder colliders for the 2 interior pillars nearest the veil
        float veilPillarR = std::max(intPillarDiam / 2.0f + 0.04f,
                                      veilSpacing * 0.65f);
        float ipSxV = -tentW / 2.0f + wallThick + 0.1f;
        float ipNxV = tentW / 2.0f - wallThick - 0.1f;
        for (int ip = 0; ip < std::min(2, intPillarCount); ++ip)
        {
            float ipZ = backZ + wallThick + static_cast<float>(ip + 1)
                        * (tentL / static_cast<float>(intPillarCount + 1));
            veilSim.addCylinderCollider(glm::vec3(ipSxV, 0.0f, ipZ), veilPillarR, intPillarH);
            veilSim.addCylinderCollider(glm::vec3(ipNxV, 0.0f, ipZ), veilPillarR, intPillarH);
        }

        veilSim.captureRestPositions();
        veilSim.rebuildLRA();
        veilCloth->syncMesh();
    }

    // =====================================================================
    // ENTRANCE SCREEN -- Animated cloth curtains at east end
    // Two curtain panels hanging from the top of the entrance, gently
    // swaying in the desert breeze. Each covers roughly half the opening.
    // =====================================================================

    {
        // Curtain material (reuse veil colors but create separate for cloth)
        auto entranceCurtainMat = m_resourceManager->createMaterial("tab_entrance_curtain");
        entranceCurtainMat->setType(MaterialType::PBR);
        entranceCurtainMat->setAlbedo(glm::vec3(0.25f, 0.12f, 0.35f));
        entranceCurtainMat->setMetallic(0.0f);
        entranceCurtainMat->setRoughness(0.85f);
        entranceCurtainMat->setDiffuseTexture(
            m_resourceManager->loadTexture(texBase + "rough_linen_diff.jpg"));
        entranceCurtainMat->setNormalMap(
            m_resourceManager->loadTexture(texBase + "rough_linen_nor_gl.jpg", true));
        entranceCurtainMat->setUvScale(1.5f);
        entranceCurtainMat->setDoubleSided(true);
        entranceCurtainMat->setEmissive(glm::vec3(0.12f, 0.04f, 0.10f));
        entranceCurtainMat->setEmissiveStrength(0.4f);

        // Use linen curtain preset for entrance cloth
        ClothPresetConfig curtainPreset = ClothPresets::linenCurtain();

        // Each curtain: grid hangs in XY plane (width along X, height along Y)
        // Width must fit inside the tent walls — the inner wall surface is at
        // ±wallInner. Each curtain covers one half of the entrance with a small
        // center gap. Margin prevents particles from spawning inside wall colliders.
        float wallInnerCurtain = tentW / 2.0f - wallThick / 2.0f;
        float curtainMargin = 0.05f;
        float curtainW = wallInnerCurtain - curtainMargin;  // ~1.975m per panel
        float curtainH = tentH - 0.2f;                      // ~4.25m tall
        float spacing = 0.4f;
        uint32_t gridW = std::max(3u, static_cast<uint32_t>(curtainW / spacing) + 1);
        uint32_t gridH = std::max(3u, static_cast<uint32_t>(curtainH / spacing) + 1);
        spacing = std::min(curtainW / static_cast<float>(gridW - 1),
                           curtainH / static_cast<float>(gridH - 1));

        ClothConfig curtainCfg = curtainPreset.solver;
        curtainCfg.width = gridW;
        curtainCfg.height = gridH;
        curtainCfg.spacing = spacing;

        // --- Left curtain ---
        {
            // Center the curtain between the south wall inner surface and the center gap
            float leftCenterX = -(wallInnerCurtain + curtainMargin) / 2.0f;
            Entity* leftCurtain = scene->createEntity("Entrance Curtain L");
            leftCurtain->transform.position = glm::vec3(0.0f, 0.0f, 0.0f);

            auto* clothComp = leftCurtain->addComponent<ClothComponent>();
            clothComp->initialize(curtainCfg, entranceCurtainMat, 77777u);
            clothComp->setPresetType(ClothPresetType::LINEN_CURTAIN);

            auto& sim = clothComp->getSimulator();

            for (uint32_t gz = 0; gz < gridH; ++gz)
            {
                for (uint32_t gx = 0; gx < gridW; ++gx)
                {
                    uint32_t idx = gz * gridW + gx;
                    float worldX = leftCenterX + (static_cast<float>(gx) -
                                   static_cast<float>(gridW - 1) * 0.5f) * spacing;
                    // Pin below the ceiling — linen ceiling bottom is at tentH - 0.075
                    float worldY = clothHangY - static_cast<float>(gz) * spacing;
                    float worldZ = frontZ + 0.25f;

                    sim.pinParticle(idx, glm::vec3(worldX, worldY, worldZ));
                }
            }

            for (uint32_t gz = 1; gz < gridH; ++gz)
            {
                for (uint32_t gx = 0; gx < gridW; ++gx)
                {
                    sim.unpinParticle(gz * gridW + gx);
                }
            }

            sim.setWind(sceneWindDir, curtainPreset.windStrength);
            sim.setDragCoefficient(curtainPreset.dragCoefficient);
            sim.setGroundPlane(0.0f);

            // Collision: ceiling + sphere colliders at entrance pillars
            sim.addPlaneCollider(glm::vec3(0, -1, 0), -(tentH - 0.1f)); // Below ceiling

            // Tent side walls as box colliders
            float halfW = wallThick / 2.0f;
            sim.addBoxCollider(
                glm::vec3(-tentW / 2.0f - halfW, 0.0f, backZ),
                glm::vec3(-tentW / 2.0f + halfW, tentH, frontZ));  // South wall
            sim.addBoxCollider(
                glm::vec3(tentW / 2.0f - halfW, 0.0f, backZ),
                glm::vec3(tentW / 2.0f + halfW, tentH, frontZ));   // North wall

            // Interior pillars: cylinder colliders for the 2 closest to the entrance.
            // Collider radius must exceed half the particle spacing so the
            // cylinder catches the mesh triangle between adjacent particles.
            float intPillarR = std::max(intPillarDiam / 2.0f + 0.04f,
                                         spacing * 0.65f);
            float ipSx = -tentW / 2.0f + wallThick + 0.1f;
            float ipNx = tentW / 2.0f - wallThick - 0.1f;
            for (int ip = intPillarCount - 1; ip >= std::max(0, intPillarCount - 2); --ip)
            {
                float ipZ = backZ + wallThick + static_cast<float>(ip + 1)
                            * (tentL / static_cast<float>(intPillarCount + 1));
                sim.addCylinderCollider(glm::vec3(ipSx, 0.0f, ipZ), intPillarR, intPillarH);
                sim.addCylinderCollider(glm::vec3(ipNx, 0.0f, ipZ), intPillarR, intPillarH);
            }

            clothComp->getSimulator().captureRestPositions();
            clothComp->getSimulator().rebuildLRA();
            clothComp->syncMesh();
        }

        // --- Right curtain ---
        {
            float rightCenterX = (wallInnerCurtain + curtainMargin) / 2.0f;
            Entity* rightCurtain = scene->createEntity("Entrance Curtain R");
            rightCurtain->transform.position = glm::vec3(0.0f, 0.0f, 0.0f);

            auto* clothComp = rightCurtain->addComponent<ClothComponent>();
            clothComp->initialize(curtainCfg, entranceCurtainMat, 99999u);
            clothComp->setPresetType(ClothPresetType::LINEN_CURTAIN);

            auto& sim = clothComp->getSimulator();

            for (uint32_t gz = 0; gz < gridH; ++gz)
            {
                for (uint32_t gx = 0; gx < gridW; ++gx)
                {
                    uint32_t idx = gz * gridW + gx;
                    float worldX = rightCenterX + (static_cast<float>(gx) -
                                   static_cast<float>(gridW - 1) * 0.5f) * spacing;
                    // Pin below the ceiling — linen ceiling bottom is at tentH - 0.075
                    float worldY = clothHangY - static_cast<float>(gz) * spacing;
                    float worldZ = frontZ + 0.25f;

                    sim.pinParticle(idx, glm::vec3(worldX, worldY, worldZ));
                }
            }

            for (uint32_t gz = 1; gz < gridH; ++gz)
            {
                for (uint32_t gx = 0; gx < gridW; ++gx)
                {
                    sim.unpinParticle(gz * gridW + gx);
                }
            }

            sim.setWind(sceneWindDir, curtainPreset.windStrength);
            sim.setDragCoefficient(curtainPreset.dragCoefficient);
            sim.setGroundPlane(0.0f);

            // Colliders: identical to left curtain (walls, ceiling, pillars)
            sim.addPlaneCollider(glm::vec3(0, -1, 0), -(tentH - 0.1f)); // Below ceiling

            float halfW = wallThick / 2.0f;
            sim.addBoxCollider(
                glm::vec3(-tentW / 2.0f - halfW, 0.0f, backZ),
                glm::vec3(-tentW / 2.0f + halfW, tentH, frontZ));  // South wall
            sim.addBoxCollider(
                glm::vec3(tentW / 2.0f - halfW, 0.0f, backZ),
                glm::vec3(tentW / 2.0f + halfW, tentH, frontZ));   // North wall

            // Collider radius must exceed half the particle spacing so the
            // cylinder catches the mesh triangle between adjacent particles.
            float intPillarR = std::max(intPillarDiam / 2.0f + 0.04f,
                                         spacing * 0.65f);
            float ipSx = -tentW / 2.0f + wallThick + 0.1f;
            float ipNx = tentW / 2.0f - wallThick - 0.1f;
            for (int ip = intPillarCount - 1; ip >= std::max(0, intPillarCount - 2); --ip)
            {
                float ipZ = backZ + wallThick + static_cast<float>(ip + 1)
                            * (tentL / static_cast<float>(intPillarCount + 1));
                sim.addCylinderCollider(glm::vec3(ipSx, 0.0f, ipZ), intPillarR, intPillarH);
                sim.addCylinderCollider(glm::vec3(ipNx, 0.0f, ipZ), intPillarR, intPillarH);
            }

            clothComp->getSimulator().captureRestPositions();
            clothComp->getSimulator().rebuildLRA();
            clothComp->syncMesh();
        }
    }

    // =====================================================================
    // HOLY OF HOLIES FURNITURE -- Ark of the Covenant (Exodus 25:10-22)
    // =====================================================================

    float arkL = 2.5f * C;  // 1.1125m
    float arkW = 1.5f * C;  // 0.6675m
    float arkH = 1.5f * C;  // 0.6675m
    float arkY = arkH / 2.0f;
    float arkZ = holyOfHoliesL / 2.0f;

    makeBox("Ark of the Covenant", {0.0f, arkY, arkZ}, {arkL, arkH, arkW}, goldMat);

    // Mercy Seat (thin gold slab on top)
    makeBox("Mercy Seat", {0.0f, arkH + 0.03f, arkZ},
            {arkL + 0.08f, 0.06f, arkW + 0.08f}, goldMat);

    // Cherubim (simplified gold shapes on the mercy seat)
    float cherubH = 0.5f;
    makeBox("Cherub Left", {-arkL * 0.3f, arkH + 0.06f + cherubH / 2.0f, arkZ},
            {0.15f, cherubH, 0.2f}, goldMat);
    makeBox("Cherub Right", {arkL * 0.3f, arkH + 0.06f + cherubH / 2.0f, arkZ},
            {0.15f, cherubH, 0.2f}, goldMat);

    // =====================================================================
    // HOLY PLACE FURNITURE
    // =====================================================================

    float hpCenterZ = veilZ + holyPlaceL / 2.0f;

    // --- Golden Lampstand / Menorah -- south side (Exodus 25:31-40) ---
    float menorahX = -tentW / 2.0f + 1.0f;
    float menorahZ = hpCenterZ;
    float menorahH = 1.5f;  // ~3 cubits tall (scholarly estimate)

    makeBox("Menorah Base", {menorahX, 0.08f, menorahZ}, {0.4f, 0.16f, 0.4f}, goldMat);
    makeBox("Menorah Shaft", {menorahX, menorahH / 2.0f, menorahZ},
            {0.08f, menorahH, 0.08f}, goldMat);

    // 7 lamps (3 pairs + center) -- spheres for lamp cups
    float branchSpacing = 0.18f;
    for (int i = -3; i <= 3; i++)
    {
        float bx = menorahX + static_cast<float>(i) * branchSpacing;
        makeSphere("Menorah Lamp " + std::to_string(i + 4),
                   {bx, menorahH, menorahZ}, {0.08f, 0.08f, 0.08f}, goldMat);
    }

    // --- Table of Showbread -- north side (Exodus 25:23-30) ---
    float tableX = tentW / 2.0f - 1.0f;
    float tableZ = hpCenterZ;
    float tableH = 1.5f * C;
    float tableL = 2.0f * C;
    float tableW_dim = 1.0f * C;

    // Table legs (4 corners)
    float legW = 0.06f;
    for (int xi = -1; xi <= 1; xi += 2)
    {
        for (int zi = -1; zi <= 1; zi += 2)
        {
            float lx = tableX + static_cast<float>(xi) * (tableL / 2.0f - legW);
            float lz = tableZ + static_cast<float>(zi) * (tableW_dim / 2.0f - legW);
            makeBox("Table Leg", {lx, tableH / 2.0f, lz}, {legW, tableH, legW}, goldMat);
        }
    }
    // Tabletop
    makeBox("Showbread Table", {tableX, tableH + 0.025f, tableZ},
            {tableL, 0.05f, tableW_dim}, goldMat);

    // 12 bread loaves (2 stacks of 6, displayed as 2 columns x 3 rows)
    for (int stack = 0; stack < 2; stack++)
    {
        float sx = tableX + (stack == 0 ? -0.12f : 0.12f);
        for (int row = 0; row < 3; row++)
        {
            float sy = tableH + 0.05f + static_cast<float>(row) * 0.08f + 0.04f;
            makeBox("Bread Loaf", {sx, sy, tableZ}, {0.15f, 0.06f, 0.12f}, woodMat);
        }
    }

    // --- Altar of Incense -- center, before the veil (Exodus 30:1-10) ---
    float incenseZ = veilZ + 0.8f;
    float incenseH = 2.0f * C;
    float incenseW_dim = 1.0f * C;

    makeBox("Incense Altar", {0.0f, incenseH / 2.0f, incenseZ},
            {incenseW_dim, incenseH, incenseW_dim}, goldMat);

    // 4 horns on top corners
    float iHornH = 0.1f;
    for (int xi = -1; xi <= 1; xi += 2)
    {
        for (int zi = -1; zi <= 1; zi += 2)
        {
            makeBox("Incense Horn",
                    {static_cast<float>(xi) * incenseW_dim * 0.4f,
                     incenseH + iHornH / 2.0f,
                     incenseZ + static_cast<float>(zi) * incenseW_dim * 0.4f},
                    {0.05f, iHornH, 0.05f}, goldMat);
        }
    }

    // =====================================================================
    // INTERIOR PILLARS -- Gold-topped wooden pillars along tent walls
    // =====================================================================

    for (int i = 0; i < intPillarCount; i++)
    {
        float z = backZ + wallThick + static_cast<float>(i + 1)
                  * (tentL / static_cast<float>(intPillarCount + 1));

        float sx = -tentW / 2.0f + wallThick + 0.1f;
        float nx = tentW / 2.0f - wallThick - 0.1f;
        std::string idx = std::to_string(i);

        // South side
        makeCylinder("S Pillar " + idx, {sx, intPillarH / 2.0f, z},
                     intPillarDiam, intPillarH, woodMat);
        makeCylinder("S Pillar Cap " + idx, {sx, intPillarH + 0.05f, z},
                     intPillarDiam + 0.08f, 0.1f, goldMat);
        makeCylinder("S Pillar Base " + idx, {sx, 0.05f, z},
                     intPillarDiam + 0.12f, 0.1f, silverMat);

        // North side
        makeCylinder("N Pillar " + idx, {nx, intPillarH / 2.0f, z},
                     intPillarDiam, intPillarH, woodMat);
        makeCylinder("N Pillar Cap " + idx, {nx, intPillarH + 0.05f, z},
                     intPillarDiam + 0.08f, 0.1f, goldMat);
        makeCylinder("N Pillar Base " + idx, {nx, 0.05f, z},
                     intPillarDiam + 0.12f, 0.1f, silverMat);
    }

    // =====================================================================
    // OUTER COURTYARD -- Fence walls (Exodus 27:9-19)
    // =====================================================================

    // Fence walls: linen cloth panels between pillars, affected by wind.
    // Stiffer than entrance curtains — taut fabric that ripples subtly.
    // All panels use the same scene-wide wind direction for consistency.

    // Cloth material for courtyard walls (copy of whiteLinenMat with doubleSided)
    auto fenceClothMat = m_resourceManager->createMaterial("tab_fence_cloth");
    fenceClothMat->setType(MaterialType::PBR);
    fenceClothMat->setAlbedo(glm::vec3(0.95f, 0.93f, 0.90f));
    fenceClothMat->setMetallic(0.0f);
    fenceClothMat->setRoughness(0.95f);   // Very rough — plain woven linen, no sheen
    fenceClothMat->setDiffuseTexture(
        m_resourceManager->loadTexture(texBase + "rough_linen_diff.jpg"));
    fenceClothMat->setNormalMap(
        m_resourceManager->loadTexture(texBase + "rough_linen_nor_gl.jpg", true));
    // No metallic-roughness texture — the packed texture had low roughness values
    // that made the linen look silver/reflective. Plain linen is uniformly rough.
    fenceClothMat->setUvScale(2.0f);
    fenceClothMat->setDoubleSided(true);

    // Helper: create a static mesh fence panel between two pillar tops.
    // Uses a flat quad instead of cloth simulation — the courtyard fence is
    // taut linen that barely moves, so full XPBD is unnecessary. This saves
    // ~80 cloth simulations per frame (~200ms CPU).
    auto makeStaticFence = [&](const std::string& name,
                               glm::vec3 topLeft, glm::vec3 topRight,
                               float height)
    {
        float panelWidth = glm::length(topRight - topLeft);
        glm::vec3 along = glm::normalize(topRight - topLeft);
        glm::vec3 up(0, 1, 0);
        glm::vec3 normal = glm::normalize(glm::cross(along, up));

        // Build a simple quad: 4 vertices, 2 triangles, double-sided
        glm::vec3 bl = topLeft + glm::vec3(0, -height, 0);  // bottom-left
        glm::vec3 br = topRight + glm::vec3(0, -height, 0); // bottom-right
        glm::vec3 tl = topLeft;                               // top-left
        glm::vec3 tr = topRight;                              // top-right

        // UV: tile texture across panel width and height
        float uScale = panelWidth / 1.0f;  // 1m per UV repeat
        float vScale = height / 1.0f;
        glm::vec3 white(1.0f);
        glm::vec3 tangent = along;
        glm::vec3 bitangent = up;

        std::vector<Vertex> vertices = {
            {bl, normal, white, {0.0f, 0.0f}, tangent, bitangent},
            {br, normal, white, {uScale, 0.0f}, tangent, bitangent},
            {tr, normal, white, {uScale, vScale}, tangent, bitangent},
            {tl, normal, white, {0.0f, vScale}, tangent, bitangent},
        };
        std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

        auto mesh = std::make_shared<Mesh>();
        mesh->upload(vertices, indices);

        Entity* entity = scene->createEntity(name);
        entity->transform.position = glm::vec3(0.0f);
        auto* mr = entity->addComponent<MeshRenderer>(mesh, fenceClothMat);
        mr->setCastsShadow(false);  // Thin fabric — no meaningful shadow
    };

    // Split long walls into segments between pillars (~2.225m each).
    auto makeFenceWall = [&](const std::string& baseName,
                             glm::vec3 start, glm::vec3 end,
                             float height, int segments)
    {
        glm::vec3 step = (end - start) / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i)
        {
            glm::vec3 segStart = start + step * static_cast<float>(i);
            glm::vec3 segEnd = segStart + step;
            makeStaticFence(baseName + " " + std::to_string(i + 1),
                            segStart, segEnd, height);
        }
    };

    // South wall: runs along Z at X = -courtW/2, 20 pillar spans
    makeFenceWall("Court South Wall",
        glm::vec3(-courtW / 2.0f, fenceH, courtWestZ),
        glm::vec3(-courtW / 2.0f, fenceH, courtEastZ),
        fenceH, 20);

    // North wall: runs along Z at X = +courtW/2, 20 pillar spans
    makeFenceWall("Court North Wall",
        glm::vec3(courtW / 2.0f, fenceH, courtWestZ),
        glm::vec3(courtW / 2.0f, fenceH, courtEastZ),
        fenceH, 20);

    // West wall: runs along X at Z = courtWestZ, 10 pillar spans
    makeFenceWall("Court West Wall",
        glm::vec3(-courtW / 2.0f, fenceH, courtWestZ),
        glm::vec3(courtW / 2.0f, fenceH, courtWestZ),
        fenceH, 10);

    // East wall: two sections flanking the gate
    const float eastSectionW = 15.0f * C;
    int eastSegments = static_cast<int>(eastSectionW / pillarSpacing);

    // East wall south section
    float eastSouthStart = -(gateW / 2.0f + eastSectionW);
    makeFenceWall("Court East Wall S",
        glm::vec3(eastSouthStart, fenceH, courtEastZ),
        glm::vec3(-gateW / 2.0f, fenceH, courtEastZ),
        fenceH, std::max(1, eastSegments));

    // East wall north section
    makeFenceWall("Court East Wall N",
        glm::vec3(gateW / 2.0f, fenceH, courtEastZ),
        glm::vec3(gateW / 2.0f + eastSectionW, fenceH, courtEastZ),
        fenceH, std::max(1, eastSegments));

    // =====================================================================
    // COURTYARD GATE -- Open (curtain pulled aside, not rendered)
    // The 20-cubit gate opening between the east wall sections is left clear.
    // =====================================================================

    // =====================================================================
    // COURTYARD PILLARS -- 60 total with silver caps and bronze bases
    // =====================================================================

    const float cPillarH = fenceH;
    const float cPillarDiam = 0.15f;
    const float capH = 0.08f;
    const float baseH = 0.08f;

    // Helper to place one courtyard pillar (post + silver cap + bronze base)
    // Shadow casting disabled -- tiny geometry creates CSM texel flicker artifacts
    auto makeCourtPillar = [&](const std::string& prefix, float x, float z)
    {
        auto* post = makeCylinder(prefix, {x, cPillarH / 2.0f, z}, cPillarDiam, cPillarH, woodMat);
        post->getComponent<MeshRenderer>()->setCastsShadow(false);
        auto* cap = makeCylinder(prefix + " Cap", {x, cPillarH + capH / 2.0f, z},
                     cPillarDiam + 0.04f, capH, silverMat);
        cap->getComponent<MeshRenderer>()->setCastsShadow(false);
        auto* base = makeCylinder(prefix + " Base", {x, baseH / 2.0f, z},
                     cPillarDiam + 0.06f, baseH, bronzeMat);
        base->getComponent<MeshRenderer>()->setCastsShadow(false);
    };

    // South wall pillars (20, along X = -courtW/2)
    for (int i = 0; i < 20; i++)
    {
        float z = courtWestZ + static_cast<float>(i) * pillarSpacing;
        makeCourtPillar("Court S " + std::to_string(i), -courtW / 2.0f, z);
    }

    // North wall pillars (20, along X = +courtW/2)
    for (int i = 0; i < 20; i++)
    {
        float z = courtWestZ + static_cast<float>(i) * pillarSpacing;
        makeCourtPillar("Court N " + std::to_string(i), courtW / 2.0f, z);
    }

    // West wall pillars (10, along Z = courtWestZ)
    for (int i = 0; i < 10; i++)
    {
        float x = -courtW / 2.0f + static_cast<float>(i) * pillarSpacing;
        makeCourtPillar("Court W " + std::to_string(i), x, courtWestZ);
    }

    // East wall pillars: 3 south of gate + 4 at gate + 3 north of gate
    for (int i = 0; i < 3; i++)
    {
        float x = -courtW / 2.0f + static_cast<float>(i) * pillarSpacing;
        makeCourtPillar("Court ES " + std::to_string(i), x, courtEastZ);
    }
    for (int i = 0; i < 4; i++)
    {
        float x = -gateW / 2.0f + static_cast<float>(i) * (gateW / 3.0f);
        makeCourtPillar("Court EG " + std::to_string(i), x, courtEastZ);
    }
    for (int i = 0; i < 3; i++)
    {
        float x = courtW / 2.0f - static_cast<float>(i) * pillarSpacing;
        makeCourtPillar("Court EN " + std::to_string(i), x, courtEastZ);
    }

    // =====================================================================
    // BRONZE ALTAR -- Altar of Burnt Offering (Exodus 27:1-8)
    // =====================================================================

    float altarZ = courtEastZ - 10.0f * C;  // ~10 cubits inside the gate
    float altarH = 3.0f * C;
    float altarSide = 5.0f * C;

    makeBox("Bronze Altar", {0.0f, altarH / 2.0f, altarZ},
            {altarSide, altarH, altarSide}, bronzeMat);

    // 4 horns on corners
    float bHornH = 0.2f;
    for (int xi = -1; xi <= 1; xi += 2)
    {
        for (int zi = -1; zi <= 1; zi += 2)
        {
            makeBox("Altar Horn",
                    {static_cast<float>(xi) * altarSide * 0.45f,
                     altarH + bHornH / 2.0f,
                     altarZ + static_cast<float>(zi) * altarSide * 0.45f},
                    {0.08f, bHornH, 0.08f}, bronzeMat);
        }
    }

    // Bronze grating at midpoint
    makeBox("Altar Grating", {0.0f, altarH / 2.0f, altarZ},
            {altarSide + 0.05f, 0.03f, altarSide + 0.05f}, bronzeMat);

    // =====================================================================
    // BRONZE LAVER -- Washing basin (Exodus 30:17-21)
    // =====================================================================

    float laverZ = (altarZ + frontZ) / 2.0f;  // Between altar and tent entrance

    // Pedestal
    makeCylinder("Laver Pedestal", {0.0f, 0.25f, laverZ}, 0.3f, 0.5f, bronzeMat);

    // Basin (squished sphere for bowl shape)
    makeSphere("Laver Basin", {0.0f, 0.65f, laverZ}, {0.7f, 0.5f, 0.7f}, bronzeMat);

    // =====================================================================
    // LIGHTING
    // =====================================================================

    // Directional light -- bright desert sun
    Entity* sunEntity = scene->createEntity("Sun");
    auto* dirLight = sunEntity->addComponent<DirectionalLightComponent>();
    dirLight->light.direction = glm::normalize(glm::vec3(0.3f, -0.8f, -0.5f));
    dirLight->light.diffuse = glm::vec3(3.0f, 2.7f, 2.2f);    // Bright desert sun
    dirLight->light.specular = glm::vec3(1.5f, 1.3f, 1.0f);
    dirLight->light.ambient = glm::vec3(0.15f, 0.13f, 0.10f);  // Low but visible: tent interior should be dim, not black

    // Menorah light (warm golden glow inside the Holy Place)
    Entity* menorahLight = scene->createEntity("Menorah Light");
    menorahLight->transform.position = glm::vec3(menorahX, menorahH + 0.2f, menorahZ);
    auto* ml = menorahLight->addComponent<PointLightComponent>();
    ml->light.diffuse = glm::vec3(1.5f, 1.2f, 0.6f);
    ml->light.specular = glm::vec3(1.0f, 0.9f, 0.5f);
    ml->light.constant = 1.0f;
    ml->light.linear = 0.45f;
    ml->light.quadratic = 0.50f;
    ml->light.castsShadow = false;  // Interior fill -- directional sun handles shadows

    // Fill light from tent entrance (confined to tent interior)
    Entity* entranceLight = scene->createEntity("Entrance Light");
    entranceLight->transform.position = glm::vec3(0.0f, tentH * 0.7f, frontZ - 0.5f);
    auto* el = entranceLight->addComponent<PointLightComponent>();
    el->light.diffuse = glm::vec3(1.0f, 0.95f, 0.8f);
    el->light.specular = glm::vec3(0.8f, 0.75f, 0.6f);
    el->light.constant = 1.0f;
    el->light.linear = 0.45f;
    el->light.quadratic = 0.50f;
    el->light.castsShadow = false;  // Interior fill -- directional sun handles shadows

    // =====================================================================
    // FIRE LIGHTS — Perpetual altar fire & gate torches
    // =====================================================================

    // Bronze Altar fire (Leviticus 6:12-13: "The fire must be kept burning
    // on the altar continuously; it must not go out.")
    Entity* altarFire = scene->createEntity("Altar Fire");
    altarFire->transform.position = glm::vec3(0.0f, altarH + 0.3f, altarZ);
    auto* af = altarFire->addComponent<PointLightComponent>();
    af->light.diffuse = glm::vec3(2.5f, 1.4f, 0.4f);   // Warm fire orange
    af->light.specular = glm::vec3(1.5f, 0.9f, 0.3f);
    af->light.constant = 1.0f;
    af->light.linear = 0.22f;
    af->light.quadratic = 0.20f;
    af->light.castsShadow = false;

    // Visible fire particles on the altar (campfire preset scaled to altar top)
    auto* altarParticles = altarFire->addComponent<ParticleEmitterComponent>();
    ParticleEmitterConfig altarFireCfg = ParticlePresets::campfire();
    altarFireCfg.emitsLight = false;         // Already have a PointLightComponent
    altarFireCfg.shapeRadius = altarSide * 0.35f;  // Spread across altar top
    altarFireCfg.shapeConeAngle = 25.0f;
    altarFireCfg.emissionRate = 200.0f;      // Large perpetual fire
    altarFireCfg.maxParticles = 600;
    altarFireCfg.startLifetimeMin = 0.5f;
    altarFireCfg.startLifetimeMax = 1.5f;
    altarFireCfg.startSpeedMin = 1.5f;
    altarFireCfg.startSpeedMax = 3.0f;
    altarFireCfg.startSizeMin = 0.08f;
    altarFireCfg.startSizeMax = 0.30f;
    altarParticles->getConfig() = altarFireCfg;

    // Gate torches — two fire braziers flanking the courtyard entrance
    for (int side = -1; side <= 1; side += 2)
    {
        Entity* torch = scene->createEntity(
            side < 0 ? "Gate Torch South" : "Gate Torch North");
        torch->transform.position = glm::vec3(
            static_cast<float>(side) * (gateW / 2.0f + 0.3f),
            fenceH + 0.2f,
            courtEastZ);
        auto* tl = torch->addComponent<PointLightComponent>();
        tl->light.diffuse = glm::vec3(1.8f, 1.0f, 0.3f);
        tl->light.specular = glm::vec3(1.0f, 0.6f, 0.2f);
        tl->light.constant = 1.0f;
        tl->light.linear = 0.35f;
        tl->light.quadratic = 0.44f;
        tl->light.castsShadow = false;

        // Visible fire particles on the torch (torchFire preset)
        auto* torchParticles = torch->addComponent<ParticleEmitterComponent>();
        ParticleEmitterConfig torchCfg = ParticlePresets::torchFire();
        torchCfg.emitsLight = false;         // Already have a PointLightComponent
        torchParticles->getConfig() = torchCfg;
    }

    // =====================================================================
    // LIGHT PROBE -- Tent interior (captures local lighting for correct IBL)
    // =====================================================================

    // --- SH Probe Grid: covers the entire scene (courtyard + tent) ---
    auto* shGrid = m_renderer->getSHProbeGrid();
    if (shGrid)
    {
        SHGridConfig gridConfig;
        // Grid covers courtyard + some margin
        gridConfig.worldMin = glm::vec3(-courtW / 2.0f - 2.0f, -0.5f, courtWestZ - 2.0f);
        gridConfig.worldMax = glm::vec3(courtW / 2.0f + 2.0f, tentH + 2.0f, courtEastZ + 5.0f);
        // ~2m probe spacing: good balance between quality and bake time.
        // Radiosity bounces re-capture the entire grid, so probe count matters.
        // 2m gives ~1000 probes instead of ~8000 at 1m.
        glm::vec3 gridSize = gridConfig.worldMax - gridConfig.worldMin;
        gridConfig.resolution = glm::ivec3(
            glm::max(2, static_cast<int>(gridSize.x / 2.0f)),
            glm::max(2, static_cast<int>(gridSize.y / 2.0f)),
            glm::max(2, static_cast<int>(gridSize.z / 2.0f)));

        shGrid->initialize(gridConfig);

        // Capture from scene renders at each probe position (direct light pass).
        // 32×32 face size is sufficient for L2 SH (only 9 coefficients).
        auto renderData = scene->collectRenderData();
        m_renderer->captureSHGrid(renderData, *m_camera, 1.0f, 32);

        // Radiosity: bake multi-bounce indirect lighting into the SH grid.
        // Each bounce re-captures the grid with the previous bounce's indirect light
        // visible in the scene, accumulating realistic light transport.
        // Uses 16×16 face size for bounces (even faster, L2 tolerates this).
        RadiosityBaker radiosity;
        RadiosityConfig radConfig;
        radConfig.maxBounces = 3;
        radConfig.convergenceThreshold = 0.02f;
        radConfig.normalBias = 0.3f;
        m_renderer->setSHNormalBias(radConfig.normalBias);
        radiosity.bake(*m_renderer, renderData, *m_camera, 1.0f, radConfig);
    }

    // Also keep the cubemap probe for specular reflections inside the tent
    auto* probeManager = m_renderer->getLightProbeManager();
    if (probeManager)
    {
        glm::vec3 probePos(0.0f, tentH * 0.5f, veilZ + holyPlaceL * 0.5f);
        AABB tentAABB;
        tentAABB.min = glm::vec3(-tentW / 2.0f - 0.5f, -0.1f, backZ - 0.5f);
        tentAABB.max = glm::vec3(tentW / 2.0f + 0.5f, tentH + 0.5f, frontZ + 0.5f);
        int probeIdx = probeManager->addProbe(probePos, tentAABB, 1.5f);
        auto renderData = scene->collectRenderData();
        m_renderer->captureLightProbe(probeIdx, renderData, *m_camera, 1.0f);
    }

    // =====================================================================
    // CAMERA -- Start outside the courtyard gate facing west
    // =====================================================================
    m_camera->setPosition(glm::vec3(0.0f, 2.5f, courtEastZ + 5.0f));
    m_camera->setYaw(-90.0f);
    m_camera->setPitch(-5.0f);

    scene->update(0.0f);

    Logger::info("Tabernacle scene ready: courtyard (60 pillars, gate, altar, laver), "
                 "tent (4 roof layers, veil, entrance), Ark, Menorah, Table, Incense Altar, "
                 "cloth demo, SH probe grid + cubemap probe");
}

} // namespace Vestige
