/// @file window.cpp
/// @brief Window implementation using GLFW and OpenGL 4.5.
#include "core/window.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Vestige
{

Window* Window::s_instance = nullptr;

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

    // Store static instance for framebuffer resize callback.
    // The user pointer is reserved for InputManager (set later).
    s_instance = this;

    // Register callbacks
    glfwSetFramebufferSizeCallback(m_handle, framebufferSizeCallback);

    // Restore saved window position and size from previous session
    restoreWindowState();

    // Set initial viewport (after restore, so framebuffer size matches)
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_handle, &framebufferWidth, &framebufferHeight);
    m_width = framebufferWidth;
    m_height = framebufferHeight;
    glViewport(0, 0, framebufferWidth, framebufferHeight);
}

Window::~Window()
{
    s_instance = nullptr;
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

void Window::framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height)
{
    if (s_instance)
    {
        s_instance->m_width = width;
        s_instance->m_height = height;
        glViewport(0, 0, width, height);
        s_instance->m_eventBus.publish(WindowResizeEvent(width, height));
        Logger::debug("Window resized to " + std::to_string(width) + "x" + std::to_string(height));
    }
}

// ---------------------------------------------------------------------------
// Window state persistence
// ---------------------------------------------------------------------------

/// @brief Returns the config file path for window state.
static std::filesystem::path getWindowStatePath()
{
    namespace fs = std::filesystem;
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    fs::path configDir;

    if (xdgConfig && xdgConfig[0] != '\0')
    {
        configDir = fs::path(xdgConfig);
    }
    else
    {
        const char* home = std::getenv("HOME");
        configDir = home ? fs::path(home) / ".config" : fs::path("/tmp");
    }

    return configDir / "vestige" / "window.json";
}

void Window::saveWindowState() const
{
    namespace fs = std::filesystem;

    if (!m_handle)
    {
        return;
    }

    // Don't save if the window is minimized (iconified) — the size would be 0x0
    if (glfwGetWindowAttrib(m_handle, GLFW_ICONIFIED))
    {
        return;
    }

    int posX = 0;
    int posY = 0;
    glfwGetWindowPos(m_handle, &posX, &posY);

    int sizeW = 0;
    int sizeH = 0;
    glfwGetWindowSize(m_handle, &sizeW, &sizeH);

    bool maximized = glfwGetWindowAttrib(m_handle, GLFW_MAXIMIZED) != 0;

    nlohmann::json state;
    state["x"] = posX;
    state["y"] = posY;
    state["width"] = sizeW;
    state["height"] = sizeH;
    state["maximized"] = maximized;

    fs::path statePath = getWindowStatePath();

    std::error_code ec;
    fs::create_directories(statePath.parent_path(), ec);

    std::ofstream file(statePath, std::ios::out | std::ios::trunc);
    if (file.is_open())
    {
        file << state.dump(2);
    }
}

void Window::restoreWindowState()
{
    namespace fs = std::filesystem;

    fs::path statePath = getWindowStatePath();
    if (!fs::exists(statePath))
    {
        return;
    }

    std::ifstream file(statePath);
    if (!file.is_open())
    {
        return;
    }

    nlohmann::json state;
    try
    {
        state = nlohmann::json::parse(file);
    }
    catch (const nlohmann::json::parse_error&)
    {
        return;
    }

    int w = state.value("width", 0);
    int h = state.value("height", 0);
    int x = state.value("x", -1);
    int y = state.value("y", -1);
    bool maximized = state.value("maximized", false);

    // Validate dimensions (must be reasonable)
    if (w < 200 || h < 200 || w > 8192 || h > 8192)
    {
        return;
    }

    // Apply size first
    glfwSetWindowSize(m_handle, w, h);

    // Apply position if valid (avoid off-screen placement)
    if (x >= 0 && y >= 0)
    {
        glfwSetWindowPos(m_handle, x, y);
    }

    // Maximize if it was maximized
    if (maximized)
    {
        glfwMaximizeWindow(m_handle);
    }

    Logger::debug("Restored window state: " + std::to_string(w) + "x"
                  + std::to_string(h) + " at (" + std::to_string(x) + ", "
                  + std::to_string(y) + ")"
                  + (maximized ? " [maximized]" : ""));
}

} // namespace Vestige
