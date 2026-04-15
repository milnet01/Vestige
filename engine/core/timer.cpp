// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file timer.cpp
/// @brief Timer implementation using std::chrono::steady_clock.
///
/// Previously used `glfwGetTime()`, which forced every Timer unit test
/// to `glfwInit()` / `glfwTerminate()` and pulled in libfontconfig /
/// libglib global caches. Those caches trip LeakSanitizer on parallel
/// ctest runs (the 88-byte libglib leak that made the launch sweep
/// flaky). steady_clock removes the windowing-system dependency from
/// the timing path entirely — Timer is now a pure utility.
#include "core/timer.h"

#include <chrono>
#include <thread>

namespace Vestige
{

namespace
{

double elapsedSecondsSince(std::chrono::steady_clock::time_point origin)
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now() - origin).count();
}

} // namespace

Timer::Timer()
    : m_origin(Clock::now())
    , m_lastFrameTime(0.0)
    , m_deltaTime(0.0f)
    , m_fps(0)
    , m_frameCount(0)
    , m_fpsTimer(0.0)
{
}

float Timer::update()
{
    double currentTime = elapsedSecondsSince(m_origin);
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
    return elapsedSecondsSince(m_origin);
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

    // Hybrid sleep-then-spin for precision without burning 100% CPU.
    // Sleep while >1ms away, then spin-wait for the final sub-millisecond.
    double frameEnd = m_lastFrameTime + m_targetFrameTime;
    double remaining = frameEnd - elapsedSecondsSince(m_origin);
    while (remaining > 0.001)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        remaining = frameEnd - elapsedSecondsSince(m_origin);
    }
    // Final spin-wait for sub-millisecond precision
    while (elapsedSecondsSince(m_origin) < frameEnd)
    {
        std::this_thread::yield();
    }
}

} // namespace Vestige
