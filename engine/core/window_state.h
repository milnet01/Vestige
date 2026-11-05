// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file window_state.h
/// @brief The saved window size and position, read without GLFW so it can be
///        tested: parsing never throws, and position checks take the monitors
///        as plain rectangles.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief A window state read from window_state.json.
struct WindowState
{
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    bool hasPosition = false;  ///< Both x and y were present as integers.
    bool maximized = false;
};

/// @brief A monitor's area in virtual-screen coordinates; may be negative.
struct MonitorRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// @brief Parses @p text. Empty when it is not a JSON object, a field has the
///        wrong type, or the size is outside 200..8192. Never throws.
std::optional<WindowState> parseWindowState(const std::string& text);

/// @brief True when the centre of the window's title bar (its top 16 pixels)
///        lies on one of @p monitors, so the user can grab it to move the
///        window. Negative coordinates are normal for a monitor to the left
///        of or above the primary one.
bool titleBarOnAMonitor(const WindowState& state, const std::vector<MonitorRect>& monitors);

} // namespace Vestige
