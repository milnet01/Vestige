// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_dropdown.h
/// @brief Combobox / select widget with popup menu.
#pragma once

#include "ui/ui_element.h"
#include "ui/ui_theme.h"

#include <string>
#include <vector>

namespace Vestige
{

class TextRenderer;

/// @brief A single option in a dropdown menu.
struct UIDropdownOption
{
    std::string value;   ///< Stable id used by callers.
    std::string label;   ///< Display text.
};

/// @brief Settings dropdown matching the design's `.dropdown` component.
///
/// 40 px tall, mono caret, hover brightens border, selected item rendered in
/// accent. The popup menu is drawn inline (z-ordered after the closed state
/// is drawn) when `open == true`. Click handling is left to the caller —
/// `selectedIndex` is the source of truth and is mutated externally.
class UIDropdown : public UIElement
{
public:
    UIDropdown();

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    std::vector<UIDropdownOption> options;
    int   selectedIndex = 0;
    bool  open          = false;
    bool  hovered       = false;

    const UITheme* theme = nullptr;
    TextRenderer*  textRenderer = nullptr;

    /// @brief Returns the currently selected option's label, or "" if out of range.
    const std::string& currentLabel() const;

    /// @brief The option row under @a local (relative to the box's top-left)
    ///        in the open list, or -1. Rows the list is too short to show
    ///        are not hit (3D_E-0746).
    int optionAt(const glm::vec2& local) const;

    /// @brief Closes the list; selects @a index and fires
    ///        `onSelectionChanged` when it names an option.
    void choose(int index);

    /// @brief Keyboard activation opens or closes the list.
    void activate() override;

    /// @brief Moves the selection one option toward @a direction, clamped,
    ///        and fires `onSelectionChanged` when it changed. Always returns
    ///        true (3D_E-0035).
    bool adjust(int direction) override;

    /// @brief A press opens a closed list. On an open list it picks the row
    ///        under the press, or closes the list when no row is there.
    void pointerPress(const glm::vec2& local) override;

    /// @brief Fired with the chosen index when the user picks an option.
    Signal<int> onSelectionChanged;

    /// @brief Draws the open list below the box. `render` does not, so that
    ///        UISystem can draw it after every canvas and no later widget
    ///        covers it (3D_E-0035).
    void renderOpenList(SpriteBatchRenderer& batch, int screenWidth, int screenHeight);

    /// @brief Top-left of the box as `render` last drew it.
    const glm::vec2& lastAbsolutePosition() const { return m_lastAbsPos; }

private:
    float visibleMenuHeight() const;
    glm::vec2 m_lastAbsPos{0.0f};  ///< Where render() last drew the box.
};

} // namespace Vestige
