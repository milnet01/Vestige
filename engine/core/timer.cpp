/// @file timer.cpp
/// @brief Timer implementation using GLFW time functions.
#include "core/timer.h"

#include <GLFW/glfw3.h>

namespace Vestige
{

Timer::Timer()
    : m_lastFrameTime(glfwGetTime())
    , m_deltaTime(0.0f)
    , m_fps(0)
    , m_frameCount(0)
    , m_fpsTimer(0.0)
{
}

float Timer::update()
{
    double currentTime = glfwGetTime();
    m_deltaTime = static_cast<float>(currentTime - m_lastFrameTime);
    m_lastFrameTime = currentTime;

    // Clamp delta time to prevent huge jumps (e.g., after a breakpoint)
    if (m_deltaTime > 0.25f)
    {
        m_deltaTime = 0.25f;
    }

    // FPS counter — update once per second
    m_frameCount++;
    m_fpsTimer += static_cast<double>(m_deltaTime);
    if (m_fpsTimer >= 1.0)
    {
        m_fps = m_frameCount;
        m_frameCount = 0;
        m_fpsTimer -= 1.0;
    }

    return m_deltaTime;
}

float Timer::getDeltaTime() const
{
    return m_deltaTime;
}

int Timer::getFps() const
{
    return m_fps;
}

double Timer::getElapsedTime() const
{
    return glfwGetTime();
}

} // namespace Vestige
