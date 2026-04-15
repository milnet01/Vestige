// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gradient_editor_widget.h
/// @brief ImGui widget for editing ColorGradient stops.
#pragma once

namespace Vestige
{

struct ColorGradient;

/// @brief Draws an interactive color gradient editor in ImGui.
///
/// Shows a horizontal gradient bar with draggable color stops. Click a stop
/// to select it and edit its color. Double-click the bar to add a new stop.
/// Right-click a stop to remove it.
///
/// @param label ImGui label/ID for the widget.
/// @param gradient The gradient to edit.
/// @param width Widget width (0 = full available width).
/// @param height Gradient bar height in pixels.
/// @return True if the gradient was modified this frame.
bool drawGradientEditor(const char* label, ColorGradient& gradient,
                        float width = 0.0f, float height = 30.0f);

} // namespace Vestige
