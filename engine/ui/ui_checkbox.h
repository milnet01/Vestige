// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_checkbox.h
/// @brief 20×20 checkbox widget — accent-filled when checked.
#pragma once

#include "ui/ui_element.h"
#include "ui/ui_theme.h"

#include <string>

namespace Vestige
{

class TextRenderer;

/// @brief Settings checkbox matching the design's `.check` component.
///
/// 20×20 box with a 1.5 px stroke; accent-filled with a checkmark in
/// `accentInk` when checked. Inline label drawn to the right.
class UICheckbox : public UIElement
{
public:
    UICheckbox();

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    bool        checked = false;
    bool        hovered = false;   ///< Drive externally from input handler.
    std::string label;

    const UITheme* theme = nullptr;
    TextRenderer*  textRenderer = nullptr;
};

} // namespace Vestige
