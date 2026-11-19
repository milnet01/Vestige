// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_checkbox.h"
#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"

#include <cmath>

namespace Vestige
{

UICheckbox::UICheckbox()
{
    interactive = true;
    size = {180.0f, 20.0f};
    m_accessible.role = UIAccessibleRole::Checkbox;
}

void UICheckbox::activate()
{
    checked = !checked;
    onClick.emit();
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

    // The click area follows the drawn box, as UIButton's follows its drawn
    // height: an authored size smaller than a scaled box would leave part of
    // the box unclickable.
    size = glm::max(size, glm::vec2(box));

    // Keyboard focus ring, drawn outside the box as UIButton draws its own.
    if (focused)
    {
        const float ringOffset = theme->focusRingOffset;
        drawBorder(batch, absPos - glm::vec2(ringOffset),
                   glm::vec2(box + 2.0f * ringOffset),
                   theme->accent, theme->focusRingThickness);
    }

    // Box.
    if (checked)
    {
        // Accent fill.
        batch.drawQuad(absPos, {box, box}, theme->accent);

        // Checkmark: the SpriteBatch can't rotate, so each stroke is a run
        // of small squares stepping one pixel diagonally. The offsets are
        // the design's, drawn for a 20 px box, and scale with the box.
        const float unit = box / 20.0f;
        const float ax = absPos.x;
        const float ay = absPos.y;
        const glm::vec4 markCol = glm::vec4(theme->accentInk, 1.0f);
        const glm::vec2 dot(2.0f * unit);
        // Short stroke, down and right from the left of the box.
        const int shortSteps = static_cast<int>(std::lround(4.0f * unit));
        for (int i = 0; i < shortSteps; ++i)
        {
            batch.drawQuad({ax + 5.0f * unit + static_cast<float>(i),
                             ay + 10.0f * unit + static_cast<float>(i)},
                            dot, markCol);
        }
        // Long stroke, up and right from the kink.
        const int longSteps = static_cast<int>(std::lround(8.0f * unit));
        for (int i = 0; i < longSteps; ++i)
        {
            batch.drawQuad({ax + 9.0f * unit + static_cast<float>(i),
                             ay + 14.0f * unit - static_cast<float>(i)},
                            dot, markCol);
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
        const float labelY = textRenderer->topForCenteredCaps(absPos.y + box * 0.5f, scale);
        textRenderer->renderText2D(label, labelX, labelY, scale,
                                    theme->textPrimary,
                                    screenWidth, screenHeight);
    }
}

} // namespace Vestige
