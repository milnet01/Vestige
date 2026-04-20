// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_interaction_prompt.h
/// @brief In-world "Press [KEY] to ACTION" prompt with distance-based fade.
#pragma once

#include "ui/ui_world_label.h"

#include <string>

namespace Vestige
{

/// @brief Floating in-world prompt of the form "Press [keyLabel] to action".
///
/// Two text fields: `keyLabel` (the binding to press, e.g. "E", "F", "LMB")
/// and `actionVerb` (what happens, e.g. "open", "use", "talk"). Render
/// composes them into "Press [keyLabel] to actionVerb".
///
/// Distance-based fade: alpha goes from full (`fadeNear` distance) to zero
/// (`fadeFar` distance). When the camera is between the two distances the
/// prompt is partially transparent, providing a smooth disengagement as the
/// player walks away from the interactable.
class UIInteractionPrompt : public UIWorldLabel
{
public:
    UIInteractionPrompt();

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief Key binding to display, e.g. "E", "F", "LMB".
    std::string keyLabel = "E";

    /// @brief Action verb, e.g. "open", "use", "pick up".
    std::string actionVerb = "use";

    /// @brief Below this distance the prompt is fully opaque.
    float fadeNear = 2.5f;

    /// @brief Beyond this distance the prompt is fully transparent (skipped).
    float fadeFar = 4.0f;

    /// @brief Computes the alpha multiplier given the camera's distance to the prompt.
    /// @return 1.0 at or below fadeNear, 0.0 at or above fadeFar, linear in between.
    float computeFadeAlpha(float distanceToCamera) const;

    /// @brief Returns the composed display string ("Press [E] to use").
    std::string composedText() const;
};

} // namespace Vestige
