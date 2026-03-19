/// @file engine.cpp
/// @brief Engine implementation — main loop and subsystem orchestration.
#include "core/engine.h"
#include "core/logger.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "resource/model.h"
#include "renderer/frame_diagnostics.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

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
        m_isRunning = false;
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
                int next = (current + 1) % 3;
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

            case GLFW_KEY_Q:
                m_isRunning = false;
                break;

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
    Logger::info("Controls: Escape=toggle editor/play, WASD=move (play mode), Mouse=look (play mode), F1=wireframe, F2=tonemapper, F3=HDR debug, F4=POM, F5=bloom, F6=SSAO, F7=AA mode, F8=color grading, F9=CSM debug, F10=auto-exposure, F11=diagnostic capture, Q=quit");
    Logger::info("Editor camera: Alt+LMB=orbit, MMB=pan, Scroll=zoom, F=focus, Numpad 1/3/7=front/right/top");
    Logger::info("Gamepad: Left stick=move, Right stick=look, LB=sprint, Triggers=up/down");
    return true;
}

void Engine::run()
{
    Logger::info("Entering main loop...");

    while (m_isRunning && !m_window->shouldClose())
    {
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

        // 3d. Check for viewport clicks (uses previous frame's viewport bounds)
        bool editorActive = m_editor && m_editor->getMode() == EditorMode::EDIT;
        if (editorActive)
        {
            m_editor->processViewportClick(m_window->getWidth(), m_window->getHeight());
        }

        // 3e. Update editor camera before rendering (uses previous frame's hover state)
        if (editorActive)
        {
            m_editor->updateEditorCamera(deltaTime);
            m_editor->applyEditorCamera(*m_camera);
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

        m_renderer->beginFrame();

        float aspectRatio = static_cast<float>(m_window->getWidth())
                          / static_cast<float>(winHeight);

        if (activeScene)
        {
            activeScene->collectRenderData(m_renderData);
            m_renderer->renderScene(m_renderData, *m_camera, aspectRatio);
        }

        // 7. Resolve MSAA, post-process, composite to output FBO
        m_renderer->endFrame(deltaTime);

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

        // 9. Window — swap buffers
        m_window->swapBuffers();
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

    // --- Optional glTF model loading ---
    auto testModel = m_resourceManager->loadModel("assets/models/test_model.glb");
    if (testModel)
    {
        Entity* modelRoot = testModel->instantiate(*scene, nullptr, "TestModel");
        modelRoot->transform.position = glm::vec3(5.0f, 0.0f, -3.0f);
        Logger::info("Loaded glTF model: " + std::to_string(testModel->getMeshCount()) + " meshes");
    }

    // Initial scene update to compute world matrices
    scene->update(0.0f);

    Logger::info("Demo scene ready: entities with components, 1 directional + 2 point lights + emissive lava + glass cube");
}

} // namespace Vestige
