// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file timer.h
/// @brief Frame timing and FPS tracking.
#pragma once

namespace Vestige
{

/// @brief Tracks frame timing, delta time, and frames per second.
class Timer
{
public:
    Timer();

    /// @brief Updates the timer. Call once per frame at the start of the loop.
    /// @return Delta time in seconds since the last frame.
    float update();

    /// @brief Gets the time elapsed since the last frame in seconds.
    /// @return Delta time in seconds.
    float getDeltaTime() const;

    /// @brief Gets the current frames per second (updated once per second).
    /// @return The FPS count.
    int getFps() const;

    /// @brief Gets the total elapsed time since the timer was created.
    /// @return Elapsed time in seconds.
    double getElapsedTime() const;

    /// @brief Sets the target frame rate cap (0 = uncapped).
    void setFrameRateCap(int fps);

    /// @brief Gets the current frame rate cap (0 = uncapped).
    int getFrameRateCap() const;

    /// @brief Waits until the target frame time has elapsed. Call after swapBuffers.
    void waitForFrameCap();

private:
    double m_lastFrameTime;
    float m_deltaTime;
    int m_fps;
    int m_frameCount;
    double m_fpsTimer;
    int m_frameRateCap = 0;         ///< Target FPS (0 = uncapped)
    double m_targetFrameTime = 0.0;  ///< 1.0 / cap (0 when uncapped)
};

} // namespace Vestige
