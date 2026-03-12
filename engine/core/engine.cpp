/// @file engine.cpp
/// @brief Engine implementation — main loop and subsystem orchestration.
#include "core/engine.h"
#include "core/logger.h"

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
    Logger::info("=== Vestige Engine v0.2.0 ===");
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

    // Create camera — start slightly above the ground looking forward
    m_camera = std::make_unique<Camera>(glm::vec3(0.0f, 1.5f, 5.0f));

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
    Logger::info("Controls: WASD=move, Mouse=look, Space/Shift=up/down, F1=wireframe, Q=quit");
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

        // 3. Input — process and update state
        processInput(deltaTime);
        m_inputManager->update();

        // 4. Renderer — draw the frame
        m_renderer->beginFrame();

        float aspectRatio = static_cast<float>(m_window->getWidth())
                          / static_cast<float>(m_window->getHeight());

        // Draw all scene objects
        for (const auto& obj : m_renderObjects)
        {
            m_renderer->drawMesh(*obj.mesh, obj.transform, *obj.material,
                                 *m_camera, aspectRatio);
        }

        // 5. Window — swap buffers
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

    // Release resources in reverse order of creation
    m_renderObjects.clear();
    m_materials.clear();
    m_meshes.clear();
    m_camera.reset();
    m_renderer.reset();
    m_inputManager.reset();
    m_timer.reset();
    m_window.reset();
    m_eventBus.clearAll();

    m_isRunning = false;
    Logger::info("Engine shutdown complete");
}

void Engine::processInput(float deltaTime)
{
    // WASD movement
    float forward = 0.0f;
    float right = 0.0f;
    float up = 0.0f;

    if (m_inputManager->isKeyDown(GLFW_KEY_W))
    {
        forward += 1.0f;
    }
    if (m_inputManager->isKeyDown(GLFW_KEY_S))
    {
        forward -= 1.0f;
    }
    if (m_inputManager->isKeyDown(GLFW_KEY_D))
    {
        right += 1.0f;
    }
    if (m_inputManager->isKeyDown(GLFW_KEY_A))
    {
        right -= 1.0f;
    }
    if (m_inputManager->isKeyDown(GLFW_KEY_SPACE))
    {
        up += 1.0f;
    }
    if (m_inputManager->isKeyDown(GLFW_KEY_LEFT_SHIFT))
    {
        up -= 1.0f;
    }

    m_camera->move(forward, right, up, deltaTime);

    // Mouse look (only when cursor is captured)
    if (m_isCursorCaptured)
    {
        glm::vec2 mouseDelta = m_inputManager->getMouseDelta();
        if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)
        {
            m_camera->rotate(mouseDelta.x, -mouseDelta.y);
        }
    }
}

void Engine::setupDemoScene()
{
    Logger::info("Setting up demo scene...");

    // --- Create materials ---

    // Ground material (dark grey)
    auto groundMat = std::make_unique<Material>();
    groundMat->name = "ground";
    groundMat->setDiffuseColor(glm::vec3(0.3f, 0.3f, 0.3f));
    groundMat->setSpecularColor(glm::vec3(0.1f, 0.1f, 0.1f));
    groundMat->setShininess(8.0f);

    // Red material (like a clay brick)
    auto redMat = std::make_unique<Material>();
    redMat->name = "red_clay";
    redMat->setDiffuseColor(glm::vec3(0.7f, 0.2f, 0.15f));
    redMat->setSpecularColor(glm::vec3(0.3f, 0.3f, 0.3f));
    redMat->setShininess(16.0f);

    // Gold material (shiny metal)
    auto goldMat = std::make_unique<Material>();
    goldMat->name = "gold";
    goldMat->setDiffuseColor(glm::vec3(0.83f, 0.69f, 0.22f));
    goldMat->setSpecularColor(glm::vec3(1.0f, 0.95f, 0.7f));
    goldMat->setShininess(128.0f);

    // Blue material (matte)
    auto blueMat = std::make_unique<Material>();
    blueMat->name = "blue_matte";
    blueMat->setDiffuseColor(glm::vec3(0.15f, 0.25f, 0.7f));
    blueMat->setSpecularColor(glm::vec3(0.2f, 0.2f, 0.2f));
    blueMat->setShininess(8.0f);

    // White material (stone-like)
    auto whiteMat = std::make_unique<Material>();
    whiteMat->name = "white_stone";
    whiteMat->setDiffuseColor(glm::vec3(0.85f, 0.85f, 0.8f));
    whiteMat->setSpecularColor(glm::vec3(0.4f, 0.4f, 0.4f));
    whiteMat->setShininess(32.0f);

    // --- Create meshes ---

    auto groundMesh = std::make_unique<Mesh>(Mesh::createPlane(20.0f));
    auto cubeMesh = std::make_unique<Mesh>(Mesh::createCube());

    // --- Assemble render objects ---

    // Ground plane
    glm::mat4 groundTransform = glm::mat4(1.0f);
    m_renderObjects.push_back({groundMesh.get(), groundMat.get(), groundTransform});

    // Center cube (gold)
    glm::mat4 cube1Transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f));
    m_renderObjects.push_back({cubeMesh.get(), goldMat.get(), cube1Transform});

    // Left cube (red, rotated)
    glm::mat4 cube2Transform = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.5f, -1.0f));
    cube2Transform = glm::rotate(cube2Transform, glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    m_renderObjects.push_back({cubeMesh.get(), redMat.get(), cube2Transform});

    // Right cube (blue, scaled taller)
    glm::mat4 cube3Transform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.75f, -1.0f));
    cube3Transform = glm::scale(cube3Transform, glm::vec3(1.0f, 1.5f, 1.0f));
    m_renderObjects.push_back({cubeMesh.get(), blueMat.get(), cube3Transform});

    // Back cube (white, large)
    glm::mat4 cube4Transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, -5.0f));
    cube4Transform = glm::scale(cube4Transform, glm::vec3(2.0f, 2.0f, 2.0f));
    cube4Transform = glm::rotate(cube4Transform, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    m_renderObjects.push_back({cubeMesh.get(), whiteMat.get(), cube4Transform});

    // --- Store ownership ---
    m_meshes.push_back(std::move(groundMesh));
    m_meshes.push_back(std::move(cubeMesh));
    m_materials.push_back(std::move(groundMat));
    m_materials.push_back(std::move(redMat));
    m_materials.push_back(std::move(goldMat));
    m_materials.push_back(std::move(blueMat));
    m_materials.push_back(std::move(whiteMat));

    // --- Set up lights ---

    // Directional light (sun)
    DirectionalLight sun;
    sun.direction = glm::vec3(-0.3f, -0.8f, -0.5f);
    sun.ambient = glm::vec3(0.15f, 0.15f, 0.18f);
    sun.diffuse = glm::vec3(0.9f, 0.85f, 0.75f);
    sun.specular = glm::vec3(1.0f, 1.0f, 1.0f);
    m_renderer->setDirectionalLight(sun);

    // Warm point light near the gold cube
    PointLight warmLight;
    warmLight.position = glm::vec3(1.5f, 2.0f, 1.5f);
    warmLight.ambient = glm::vec3(0.02f, 0.02f, 0.01f);
    warmLight.diffuse = glm::vec3(0.9f, 0.7f, 0.3f);
    warmLight.specular = glm::vec3(1.0f, 0.9f, 0.6f);
    warmLight.constant = 1.0f;
    warmLight.linear = 0.14f;
    warmLight.quadratic = 0.07f;
    m_renderer->addPointLight(warmLight);

    // Cool point light on the other side
    PointLight coolLight;
    coolLight.position = glm::vec3(-2.0f, 2.5f, 2.0f);
    coolLight.ambient = glm::vec3(0.01f, 0.01f, 0.02f);
    coolLight.diffuse = glm::vec3(0.3f, 0.5f, 0.9f);
    coolLight.specular = glm::vec3(0.5f, 0.7f, 1.0f);
    coolLight.constant = 1.0f;
    coolLight.linear = 0.14f;
    coolLight.quadratic = 0.07f;
    m_renderer->addPointLight(coolLight);

    Logger::info("Demo scene ready: " + std::to_string(m_renderObjects.size())
        + " objects, 1 directional + 2 point lights");
}

} // namespace Vestige
