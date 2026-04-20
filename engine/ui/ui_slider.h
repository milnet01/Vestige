// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_slider.h
/// @brief Horizontal slider widget — track + fill + draggable thumb + value readout.
#pragma once

#include "ui/ui_element.h"
#include "ui/ui_theme.h"

#include <functional>
#include <string>

namespace Vestige
{

class TextRenderer;

/// @brief Settings slider matching the design's `.slider` component.
///
/// The widget renders a track (4 px), an accent-coloured fill, a 16 px square
/// thumb with a 2 px accent ring, and a right-aligned mono value readout.
/// Dragging is handled externally — the host wires `value` to a settings
/// state and calls the slider purely for rendering.
class UISlider : public UIElement
{
public:
    UISlider();

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    float minValue = 0.0f;
    float maxValue = 100.0f;
    float value    = 50.0f;

    /// @brief Optional formatter for the value readout (e.g. `[](float v){...}`).
    /// If unset, defaults to integer percent ("62 %").
    std::function<std::string(float)> formatter;

    /// @brief Optional tick count drawn within the track. 0 = no ticks.
    int ticks = 0;

    const UITheme* theme = nullptr;
    TextRenderer*  textRenderer = nullptr;

    /// @brief Returns the normalised fill ratio in [0, 1].
    float ratio() const;
};

} // namespace Vestige
