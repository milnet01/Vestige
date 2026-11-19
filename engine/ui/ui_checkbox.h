// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_checkbox.h
/// @brief Checkbox widget — accent-filled when checked.
#pragma once

#include "ui/ui_element.h"
#include "ui/ui_theme.h"

#include <string>

namespace Vestige
{

class TextRenderer;

/// @brief Settings checkbox matching the design's `.check` component.
///
/// A `UITheme::checkboxSize` box with a stroke; accent-filled with a checkmark in
/// `accentInk` when checked. Inline label drawn to the right.
class UICheckbox : public UIElement
{
public:
    UICheckbox();

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief Toggles `checked`, then fires `onClick` (3D_E-0746).
    void activate() override;

    bool        checked = false;
    bool        hovered = false;   ///< Drive externally from input handler.
    std::string label;

    const UITheme* theme = nullptr;
    TextRenderer*  textRenderer = nullptr;
};

} // namespace Vestige
