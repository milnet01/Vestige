// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_interaction_prompt.h"
#include "renderer/camera.h"
#include "renderer/text_renderer.h"
#include "ui/ui_world_projection.h"

#include <algorithm>

namespace Vestige
{

UIInteractionPrompt::UIInteractionPrompt()
{
    // Slightly higher screen offset than the base label — interaction prompts
    // typically sit further above their target (door handle, item) so they
    // don't visually overlap the geometry being interacted with.
    screenOffset = {0.0f, -32.0f};
    color = {1.0f, 1.0f, 1.0f};
}

float UIInteractionPrompt::computeFadeAlpha(float distanceToCamera) const
{
    if (distanceToCamera <= fadeNear) return 1.0f;
    if (distanceToCamera >= fadeFar)  return 0.0f;
    const float t = (distanceToCamera - fadeNear) / (fadeFar - fadeNear);
    return std::clamp(1.0f - t, 0.0f, 1.0f);
}

std::string UIInteractionPrompt::composedText() const
{
    return "Press [" + keyLabel + "] to " + actionVerb;
}

void UIInteractionPrompt::render(SpriteBatchRenderer& /*batch*/,
                                  const glm::vec2& /*parentOffset*/,
                                  int screenWidth, int screenHeight)
{
    if (!visible || !textRenderer || !camera) return;
    if (screenWidth <= 0 || screenHeight <= 0) return;

    // Distance fade — short-circuit before doing any projection work.
    const float distance = glm::length(worldPosition - camera->getPosition());
    const float alpha = computeFadeAlpha(distance);
    if (alpha <= 0.0f) return;

    const float aspect = static_cast<float>(screenWidth)
                        / static_cast<float>(screenHeight);
    const glm::mat4 vp = camera->getProjectionMatrix(aspect)
                       * camera->getViewMatrix();

    const auto proj = projectWorldToScreen(worldPosition, vp,
                                            screenWidth, screenHeight);
    if (!proj.visible) return;

    const glm::vec2 finalPos = proj.screenPos + screenOffset;
    const glm::vec3 fadedColor = color * alpha;
    textRenderer->renderText2D(composedText(), finalPos.x, finalPos.y,
                                scale, fadedColor, screenWidth, screenHeight);
}

} // namespace Vestige
