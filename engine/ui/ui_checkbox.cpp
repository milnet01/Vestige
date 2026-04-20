// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_checkbox.h"
#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"

namespace Vestige
{

UICheckbox::UICheckbox()
{
    interactive = true;
    size = {180.0f, 20.0f};
}

namespace
{
void drawBorder(SpriteBatchRenderer& batch, const glm::vec2& pos,
                const glm::vec2& sz, const glm::vec4& color, float thickness)
{
    if (color.a <= 0.0f || thickness <= 0.0f) return;
    batch.drawQuad(pos, {sz.x, thickness}, color);
    batch.drawQuad({pos.x, pos.y + sz.y - thickness}, {sz.x, thickness}, color);
    batch.drawQuad(pos, {thickness, sz.y}, color);
    batch.drawQuad({pos.x + sz.x - thickness, pos.y}, {thickness, sz.y}, color);
}
} // namespace

void UICheckbox::render(SpriteBatchRenderer& batch,
                        const glm::vec2& parentOffset,
                        int screenWidth, int screenHeight)
{
    if (!visible || theme == nullptr) return;

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    const float box = theme->checkboxSize;

    // Box.
    if (checked)
    {
        // Accent fill.
        batch.drawQuad(absPos, {box, box}, theme->accent);

        // Checkmark — drawn as two short rectangles forming a "✓" via a
        // pair of rotated thin lines. The SpriteBatch can't rotate, so we
        // approximate with two diagonal-stepping quads (good enough at 20px).
        const float ax = absPos.x;
        const float ay = absPos.y;
        const glm::vec4 markCol = glm::vec4(theme->accentInk, 1.0f);
        // Short stroke (4 px down + right from box.left + 25%, .50% down).
        for (int i = 0; i < 4; ++i)
        {
            batch.drawQuad({ax + 5.0f + static_cast<float>(i),
                             ay + 10.0f + static_cast<float>(i)},
                            {2.0f, 2.0f}, markCol);
        }
        // Long stroke (8 px up + right from the kink).
        for (int i = 0; i < 8; ++i)
        {
            batch.drawQuad({ax + 9.0f + static_cast<float>(i),
                             ay + 14.0f - static_cast<float>(i)},
                            {2.0f, 2.0f}, markCol);
        }
    }
    else
    {
        // Empty 1.5-px stroked box; hover brightens the stroke.
        const glm::vec4 stroke = hovered
            ? glm::vec4(theme->textPrimary, 1.0f)
            : theme->panelStrokeStrong;
        drawBorder(batch, absPos, {box, box}, stroke, theme->checkboxStroke);
    }

    // Label to the right (12 px gap per design).
    if (textRenderer != nullptr && !label.empty())
    {
        const float scale = 0.36f;
        const float labelX = absPos.x + box + 12.0f;
        const float labelY = absPos.y + box * 0.5f + 5.0f;
        textRenderer->renderText2D(label, labelX, labelY, scale,
                                    theme->textPrimary,
                                    screenWidth, screenHeight);
    }
}

} // namespace Vestige
