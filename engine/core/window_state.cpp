// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file window_state.cpp
/// @brief Saved window state parsing and position check (no GLFW).
#include "core/window_state.h"

#include <nlohmann/json.hpp>

namespace Vestige
{

std::optional<WindowState> parseWindowState(const std::string& text)
{
    // Every failure is a "no saved state": a malformed or hand-edited file
    // used to throw type_error out of Window's constructor and end start-up.
    const nlohmann::json state = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!state.is_object())
    {
        return std::nullopt;
    }
    auto intField = [&state](const char* key) -> std::optional<int>
    {
        const auto it = state.find(key);
        if (it == state.end() || !it->is_number_integer())
        {
            return std::nullopt;
        }
        return it->get<int>();
    };

    const auto w = intField("width");
    const auto h = intField("height");
    if (!w || !h || *w < 200 || *h < 200 || *w > 8192 || *h > 8192)
    {
        return std::nullopt;
    }
    WindowState out;
    out.width = *w;
    out.height = *h;

    const auto x = intField("x");
    const auto y = intField("y");
    out.hasPosition = x && y;
    out.x = x.value_or(0);
    out.y = y.value_or(0);

    const auto maximized = state.find("maximized");
    out.maximized = maximized != state.end() && maximized->is_boolean() && maximized->get<bool>();
    return out;
}

bool titleBarOnAMonitor(const WindowState& state, const std::vector<MonitorRect>& monitors)
{
    const long cx = static_cast<long>(state.x) + state.width / 2;
    const long cy = static_cast<long>(state.y) + 8;
    for (const MonitorRect& m : monitors)
    {
        if (cx >= m.x && cx < static_cast<long>(m.x) + m.width
            && cy >= m.y && cy < static_cast<long>(m.y) + m.height)
        {
            return true;
        }
    }
    return false;
}

} // namespace Vestige
