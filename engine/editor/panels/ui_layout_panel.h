// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_layout_panel.h
/// @brief Editor panel for inspecting + tweaking in-game UI canvases + theme.
///
/// Phase 9C closeout. Shows the element tree of a `UICanvas` passed in from
/// game code, exposes each element's position / size / anchor / visibility
/// via ImGui widgets, and offers a full live color-picker surface over the
/// active `UITheme` so theming changes propagate instantly across the menu
/// prefabs + HUD widgets.
///
/// **Out of scope for this panel (follow-up enhancements):**
/// - Drag-place widget palette (needs viewport mouse capture + live drag math)
/// - JSON canvas serialisation (needs per-element-type reflection)
/// Both land cleanly after the editor's ImGui viewport is factored out of
/// `editor.cpp` — currently the main viewport grabs all mouse events.
#pragma once

namespace Vestige
{

class UICanvas;
struct UITheme;

/// @brief Editor panel for UI-layout inspection + live theme tweaking.
class UILayoutPanel
{
public:
    /// @brief Draws the panel contents inside its own ImGui window.
    /// @param canvas Target canvas to inspect (may be null — panel shows an
    ///               empty-state message in that case).
    /// @param theme  Active theme (mutable — color pickers write through).
    void draw(UICanvas* canvas, UITheme* theme);

    /// @brief Whether the panel is currently visible.
    bool isOpen() const { return m_open; }

    /// @brief Toggle panel visibility.
    void setOpen(bool open) { m_open = open; }

private:
    bool m_open = false;
    int  m_selectedElement = -1;   ///< Index into the canvas's element list (−1 = none).
};

} // namespace Vestige
