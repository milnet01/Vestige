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
    double rawDelta = currentTime - m_lastFrameTime;
    m_lastFrameTime = currentTime;

    // Clamp delta time to prevent huge jumps (e.g., after a breakpoint)
    m_deltaTime = static_cast<float>(rawDelta);
    if (m_deltaTime > 0.25f)
    {
        m_deltaTime = 0.25f;
    }

    // FPS counter — use raw (unclamped) elapsed time for accurate measurement
    m_frameCount++;
    m_fpsTimer += rawDelta;
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

void Timer::setFrameRateCap(int fps)
{
    m_frameRateCap = fps;
    m_targetFrameTime = (fps > 0) ? (1.0 / static_cast<double>(fps)) : 0.0;
}

int Timer::getFrameRateCap() const
{
    return m_frameRateCap;
}

void Timer::waitForFrameCap()
{
    if (m_targetFrameTime <= 0.0)
    {
        return;
    }

    // Spin-wait for precision (sleep is too coarse for frame timing)
    double frameEnd = m_lastFrameTime + m_targetFrameTime;
    while (glfwGetTime() < frameEnd)
    {
        // Yield to OS briefly to avoid burning 100% CPU
        // glfwWaitEventsTimeout is too coarse; a short busy-wait is standard practice
    }
}

} // namespace Vestige
