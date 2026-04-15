// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file curve_editor_widget.h
/// @brief ImGui widget for editing AnimationCurve keyframes.
#pragma once

namespace Vestige
{

struct AnimationCurve;

/// @brief Draws an interactive curve editor in ImGui.
///
/// The widget shows a plot area where keyframes can be dragged, added (double-click),
/// and removed (right-click). Returns true if the curve was modified.
///
/// @param label ImGui label/ID for the widget.
/// @param curve The curve to edit.
/// @param width Widget width (0 = full available width).
/// @param height Widget height in pixels.
/// @return True if the curve was modified this frame.
bool drawCurveEditor(const char* label, AnimationCurve& curve,
                     float width = 0.0f, float height = 100.0f);

} // namespace Vestige
