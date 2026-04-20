// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_world_label.h"
#include "renderer/camera.h"
#include "renderer/text_renderer.h"
#include "ui/ui_world_projection.h"

namespace Vestige
{

void UIWorldLabel::render(SpriteBatchRenderer& /*batch*/,
                           const glm::vec2& /*parentOffset*/,
                           int screenWidth, int screenHeight)
{
    if (!visible || !textRenderer || !camera || text.empty()) return;
    if (screenWidth <= 0 || screenHeight <= 0) return;

    const float aspect = static_cast<float>(screenWidth)
                        / static_cast<float>(screenHeight);
    const glm::mat4 vp = camera->getProjectionMatrix(aspect)
                       * camera->getViewMatrix();

    const auto proj = projectWorldToScreen(worldPosition, vp,
                                            screenWidth, screenHeight);
    if (!proj.visible) return;

    const glm::vec2 finalPos = proj.screenPos + screenOffset;
    textRenderer->renderText2D(text, finalPos.x, finalPos.y, scale, color,
                                screenWidth, screenHeight);
}

} // namespace Vestige
