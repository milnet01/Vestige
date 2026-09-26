// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file frame_diagnostics.h
/// @brief Frame capture and diagnostic report for visual analysis.
#pragma once

#include "renderer/renderer.h"
#include "renderer/camera.h"

#include <string>

namespace Vestige
{

/// @brief Captures the current frame and writes a diagnostic report.
///
/// Saves a screenshot (PNG) and a text report containing render state,
/// camera info, culling stats, lighting, and pixel brightness analysis.
/// Designed to give objective data about the rendered frame for analysis
/// when visual inspection alone is insufficient.
class FrameDiagnostics
{
public:
    /// @brief Captures the current default framebuffer to a PNG and writes a text report.
    /// @param renderer The renderer (for state and culling stats).
    /// @param camera The camera (for position/direction/FOV).
    /// @param windowWidth Framebuffer width in pixels.
    /// @param windowHeight Framebuffer height in pixels.
    /// @param fps Current frames per second.
    /// @param deltaTime Current frame delta time.
    /// @param outputDir Directory to save files (defaults to ~/Pictures/Screenshots/).
    /// @return Path to the saved screenshot, or empty string on failure.
    static std::string capture(const Renderer& renderer,
                               const Camera& camera,
                               int windowWidth, int windowHeight,
                               int fps, float deltaTime,
                               const std::string& outputDir = "");

    /// @brief Captures with a custom filename (for automated visual testing).
    /// @param outputDir Directory to save files.
    /// @param basename Filename without extension (e.g. "01_gate_exterior_rot045").
    /// @return Path to the saved screenshot, or empty string on failure.
    static std::string captureNamed(const Renderer& renderer,
                                     const Camera& camera,
                                     int windowWidth, int windowHeight,
                                     int fps, float deltaTime,
                                     const std::string& outputDir,
                                     const std::string& basename);

    /// @brief Writes the current default framebuffer to a PNG, nothing else.
    ///
    /// No text report and no pixel statistics: this is the per-frame path
    /// for --demo-capture, which writes hundreds of frames in a row.
    /// @return True if the PNG was written.
    static bool savePng(const std::string& pngPath, int width, int height);
};

} // namespace Vestige
