/// @file engine.cpp
/// @brief Engine implementation — main loop and subsystem orchestration.
#include "core/engine.h"
#include "core/logger.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"

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
    Logger::info("=== Vestige Engine v0.3.0 ===");
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

    // Capture cursor for FPS-style controls
    m_window->setCursorEnabled(false);
    m_isCursorCaptured = true;

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

        switch (event.keyCode)
        {
            case GLFW_KEY_ESCAPE:
                m_isCursorCaptured = !m_isCursorCaptured;
                m_window->setCursorEnabled(!m_isCursorCaptured);
                m_controller->setEnabled(m_isCursorCaptured);
                break;

            case GLFW_KEY_F1:
                m_renderer->setWireframeMode(!m_renderer->isWireframeMode());
                break;

            case GLFW_KEY_Q:
                m_isRunning = false;
                break;
        }
    });

    // Subscribe to scroll for FOV zoom
    m_eventBus.subscribe<MouseScrollEvent>([this](const MouseScrollEvent& event)
    {
        m_camera->adjustFov(static_cast<float>(event.yOffset));
    });

    m_isRunning = true;
    Logger::info("Engine initialized successfully");
    Logger::info("Controls: WASD=move, Mouse=look, Space/Shift=up/down, LCtrl=sprint, F1=wireframe, Q=quit");
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

        // 4. Scene — update entities and components
        m_sceneManager->update(deltaTime);

        // 5. Controller — process input and update camera
        Scene* activeScene = m_sceneManager->getActiveScene();
        std::vector<AABB> colliders;
        if (activeScene)
        {
            colliders = activeScene->collectColliders();
        }
        m_controller->update(deltaTime, colliders);

        // 6. Renderer — draw the frame
        m_renderer->beginFrame();

        float aspectRatio = static_cast<float>(m_window->getWidth())
                          / static_cast<float>(m_window->getHeight());

        if (activeScene)
        {
            SceneRenderData renderData = activeScene->collectRenderData();
            m_renderer->renderScene(renderData, *m_camera, aspectRatio);
        }

        // 7. Resolve MSAA and draw to screen
        m_renderer->endFrame();

        // 8. Window — swap buffers
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
    auto planeMesh = m_resourceManager->getPlaneMesh(20.0f);

    auto groundMat = m_resourceManager->createMaterial("ground");
    groundMat->setDiffuseColor(glm::vec3(0.3f, 0.3f, 0.3f));
    groundMat->setSpecularColor(glm::vec3(0.1f));
    groundMat->setShininess(8.0f);
    groundMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/Texturelabs_Stone_138M.jpg"));

    auto goldMat = m_resourceManager->createMaterial("gold");
    goldMat->setDiffuseColor(glm::vec3(0.83f, 0.69f, 0.22f));
    goldMat->setSpecularColor(glm::vec3(1.0f, 0.95f, 0.7f));
    goldMat->setShininess(128.0f);
    goldMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/Texturelabs_Metal_124M.jpg"));

    auto redMat = m_resourceManager->createMaterial("red_clay");
    redMat->setDiffuseColor(glm::vec3(0.7f, 0.2f, 0.15f));
    redMat->setSpecularColor(glm::vec3(0.3f));
    redMat->setShininess(16.0f);
    redMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/Texturelabs_Brick_124M.jpg"));

    auto blueMat = m_resourceManager->createMaterial("blue_matte");
    blueMat->setDiffuseColor(glm::vec3(0.15f, 0.25f, 0.7f));
    blueMat->setSpecularColor(glm::vec3(0.2f));
    blueMat->setShininess(8.0f);
    blueMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/Texturelabs_Glass_120M.jpg"));

    auto whiteMat = m_resourceManager->createMaterial("white_stone");
    whiteMat->setDiffuseColor(glm::vec3(0.85f, 0.85f, 0.8f));
    whiteMat->setSpecularColor(glm::vec3(0.4f));
    whiteMat->setShininess(32.0f);
    whiteMat->setDiffuseTexture(m_resourceManager->loadTexture("assets/textures/Texturelabs_Grunge_207M.jpg"));

    // --- Ground ---
    Entity* ground = scene->createEntity("Ground");
    ground->addComponent<MeshRenderer>(planeMesh, groundMat);
    // No collision bounds for the ground (we handle ground via height clamping)

    // --- Cubes ---
    Entity* goldCube = scene->createEntity("Gold Cube");
    goldCube->transform.position = glm::vec3(0.0f, 0.5f, 0.0f);
    auto* goldRenderer = goldCube->addComponent<MeshRenderer>(cubeMesh, goldMat);
    goldRenderer->setBounds(AABB::unitCube());

    Entity* redCube = scene->createEntity("Red Cube");
    redCube->transform.position = glm::vec3(-3.0f, 0.5f, -1.0f);
    redCube->transform.rotation = glm::vec3(0.0f, 30.0f, 0.0f);
    auto* redRenderer = redCube->addComponent<MeshRenderer>(cubeMesh, redMat);
    redRenderer->setBounds(AABB::unitCube());

    Entity* blueCube = scene->createEntity("Blue Cube");
    blueCube->transform.position = glm::vec3(3.0f, 0.75f, -1.0f);
    blueCube->transform.scale = glm::vec3(1.0f, 1.5f, 1.0f);
    auto* blueRenderer = blueCube->addComponent<MeshRenderer>(cubeMesh, blueMat);
    blueRenderer->setBounds(AABB::unitCube());

    Entity* whiteCube = scene->createEntity("White Cube");
    whiteCube->transform.position = glm::vec3(0.0f, 1.0f, -5.0f);
    whiteCube->transform.scale = glm::vec3(2.0f, 2.0f, 2.0f);
    whiteCube->transform.rotation = glm::vec3(0.0f, 45.0f, 0.0f);
    auto* whiteRenderer = whiteCube->addComponent<MeshRenderer>(cubeMesh, whiteMat);
    whiteRenderer->setBounds(AABB::unitCube());

    // --- Lights ---
    Entity* sun = scene->createEntity("Sun");
    auto* dirLight = sun->addComponent<DirectionalLightComponent>();
    dirLight->light.direction = glm::vec3(-0.3f, -0.8f, -0.5f);
    dirLight->light.ambient = glm::vec3(0.15f, 0.15f, 0.18f);
    dirLight->light.diffuse = glm::vec3(0.9f, 0.85f, 0.75f);
    dirLight->light.specular = glm::vec3(1.0f);

    Entity* warmLight = scene->createEntity("Warm Light");
    warmLight->transform.position = glm::vec3(1.5f, 2.0f, 1.5f);
    auto* warmPL = warmLight->addComponent<PointLightComponent>();
    warmPL->light.ambient = glm::vec3(0.02f, 0.02f, 0.01f);
    warmPL->light.diffuse = glm::vec3(0.9f, 0.7f, 0.3f);
    warmPL->light.specular = glm::vec3(1.0f, 0.9f, 0.6f);
    warmPL->light.linear = 0.14f;
    warmPL->light.quadratic = 0.07f;

    Entity* coolLight = scene->createEntity("Cool Light");
    coolLight->transform.position = glm::vec3(-2.0f, 2.5f, 2.0f);
    auto* coolPL = coolLight->addComponent<PointLightComponent>();
    coolPL->light.ambient = glm::vec3(0.01f, 0.01f, 0.02f);
    coolPL->light.diffuse = glm::vec3(0.3f, 0.5f, 0.9f);
    coolPL->light.specular = glm::vec3(0.5f, 0.7f, 1.0f);
    coolPL->light.linear = 0.14f;
    coolPL->light.quadratic = 0.07f;

    // Initial scene update to compute world matrices
    scene->update(0.0f);

    Logger::info("Demo scene ready: entities with components, 1 directional + 2 point lights");
}

} // namespace Vestige
