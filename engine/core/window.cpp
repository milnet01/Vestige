/// @file window.cpp
/// @brief Window implementation using GLFW and OpenGL 4.5.
#include "core/window.h"
#include "core/logger.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace Vestige
{

Window::Window(const WindowConfig& config, EventBus& eventBus)
    : m_handle(nullptr)
    , m_width(config.width)
    , m_height(config.height)
    , m_eventBus(eventBus)
{
    Logger::info("Initializing GLFW...");
    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Request OpenGL 4.5 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

#ifdef VESTIGE_DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    Logger::info("Creating window: " + config.title + " ("
        + std::to_string(m_width) + "x" + std::to_string(m_height) + ")");

    m_handle = glfwCreateWindow(m_width, m_height, config.title.c_str(), nullptr, nullptr);
    if (!m_handle)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(m_handle);

    // Load OpenGL functions via glad
    int version = gladLoadGL(glfwGetProcAddress);
    if (!version)
    {
        glfwDestroyWindow(m_handle);
        glfwTerminate();
        throw std::runtime_error("Failed to initialize OpenGL via glad");
    }

    Logger::info("OpenGL loaded: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
    Logger::info("GPU: " + std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))));

    // VSync
    glfwSwapInterval(config.isVsyncEnabled ? 1 : 0);

    // Store pointer to this Window instance for callbacks
    glfwSetWindowUserPointer(m_handle, this);

    // Register callbacks
    glfwSetFramebufferSizeCallback(m_handle, framebufferSizeCallback);

    // Set initial viewport
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_handle, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
}

Window::~Window()
{
    if (m_handle)
    {
        glfwDestroyWindow(m_handle);
        Logger::debug("Window destroyed");
    }
    glfwTerminate();
    Logger::debug("GLFW terminated");
}

void Window::pollEvents()
{
    glfwPollEvents();
}

void Window::swapBuffers()
{
    glfwSwapBuffers(m_handle);
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_handle);
}

int Window::getWidth() const
{
    return m_width;
}

int Window::getHeight() const
{
    return m_height;
}

GLFWwindow* Window::getHandle() const
{
    return m_handle;
}

void Window::setCursorEnabled(bool isEnabled)
{
    glfwSetInputMode(m_handle, GLFW_CURSOR,
        isEnabled ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self)
    {
        self->m_width = width;
        self->m_height = height;
        glViewport(0, 0, width, height);
        self->m_eventBus.publish(WindowResizeEvent(width, height));
        Logger::debug("Window resized to " + std::to_string(width) + "x" + std::to_string(height));
    }
}

} // namespace Vestige
