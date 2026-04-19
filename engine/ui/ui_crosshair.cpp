// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_crosshair.h"
#include "ui/sprite_batch_renderer.h"

namespace Vestige
{

void UICrosshair::render(SpriteBatchRenderer& batch,
                          const glm::vec2& /*parentOffset*/,
                          int screenWidth, int screenHeight)
{
    if (!visible) return;

    // The crosshair always centres on the viewport — anchor / position fields
    // on the base UIElement are intentionally ignored. This matches first-
    // person aiming reticle conventions where the reticle is the screen's
    // optical centre, not an anchored UI element.
    const float cx = static_cast<float>(screenWidth)  * 0.5f;
    const float cy = static_cast<float>(screenHeight) * 0.5f;
    const float halfThick = thickness * 0.5f;

    // Top arm
    batch.drawQuad({cx - halfThick, cy - centreGap - armLength},
                    {thickness, armLength}, color);
    // Bottom arm
    batch.drawQuad({cx - halfThick, cy + centreGap},
                    {thickness, armLength}, color);
    // Left arm
    batch.drawQuad({cx - centreGap - armLength, cy - halfThick},
                    {armLength, thickness}, color);
    // Right arm
    batch.drawQuad({cx + centreGap, cy - halfThick},
                    {armLength, thickness}, color);
}

} // namespace Vestige
