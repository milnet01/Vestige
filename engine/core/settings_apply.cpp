// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_apply.cpp
/// @brief Apply layer between Settings and engine subsystems — display.
#include "core/settings_apply.h"

#include "core/settings.h"
#include "core/window.h"

namespace Vestige
{

WindowDisplaySink::WindowDisplaySink(Window& window)
    : m_window(window)
{
}

void WindowDisplaySink::setVideoMode(int width, int height,
                                     bool fullscreen, bool vsync)
{
    m_window.setVideoMode(width, height, fullscreen, vsync);
}

void applyDisplay(const DisplaySettings& display, DisplayApplySink& sink)
{
    sink.setVideoMode(display.windowWidth,
                      display.windowHeight,
                      display.fullscreen,
                      display.vsync);
}

} // namespace Vestige
