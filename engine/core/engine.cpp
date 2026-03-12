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
    Logger::info("=== Vestige Engine v0.1.0 ===");
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

    // Create camera
    m_camera = std::make_unique<Camera>(glm::vec3(0.0f, 0.0f, 3.0f));

    // Create demo cube
    m_cubeMesh = std::make_unique<Mesh>(Mesh::createCube());

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
        // Escape toggles cursor capture
        if (event.keyCode == GLFW_KEY_ESCAPE && !event.isRepeat)
        {
            m_isCursorCaptured = !m_isCursorCaptured;
            m_window->setCursorEnabled(!m_isCursorCaptured);
        }
    });

    // Subscribe to scroll for FOV zoom
    m_eventBus.subscribe<MouseScrollEvent>([this](const MouseScrollEvent& event)
    {
        m_camera->adjustFov(static_cast<float>(event.yOffset));
    });

    m_isRunning = true;
    Logger::info("Engine initialized successfully");
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

        // Draw the demo cube at the origin
        glm::mat4 model = glm::mat4(1.0f);
        m_renderer->drawMesh(*m_cubeMesh, model, *m_camera, aspectRatio);

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
    m_cubeMesh.reset();
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
            // Invert Y because screen Y goes down, but we want up to be positive pitch
            m_camera->rotate(mouseDelta.x, -mouseDelta.y);
        }
    }
}

} // namespace Vestige
