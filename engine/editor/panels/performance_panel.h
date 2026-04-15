// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file performance_panel.h
/// @brief ImGui panel for real-time performance profiling dashboard.
#pragma once

namespace Vestige
{

class PerformanceProfiler;
class Renderer;
class Timer;
class Window;

/// @brief Real-time performance dashboard with frame time graph, GPU/CPU timing, memory, draw calls.
class PerformancePanel
{
public:
    /// @brief Draws the performance panel.
    /// @param profiler The performance profiler to read data from.
    /// @param renderer The renderer (for CullingStats).
    /// @param timer Optional timer for frame cap controls.
    /// @param window Optional window for vsync controls.
    void draw(PerformanceProfiler& profiler, const Renderer* renderer,
              Timer* timer = nullptr, Window* window = nullptr);

    bool isOpen() const { return m_open; }
    void setOpen(bool open) { m_open = open; }
    void toggleOpen() { m_open = !m_open; }

private:
    void drawOverviewTab(PerformanceProfiler& profiler,
                         Timer* timer, Window* window);
    void drawGpuTab(PerformanceProfiler& profiler);
    void drawCpuTab(PerformanceProfiler& profiler);
    void drawMemoryTab(PerformanceProfiler& profiler);
    void drawDrawCallsTab(const Renderer* renderer);

    bool m_open = false;
};

} // namespace Vestige
