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
};

} // namespace Vestige
