// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_keybind_row.h"
#include "localization/localization_service.h"
#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"

#include <string>

namespace Vestige
{

UIKeybindRow::UIKeybindRow()
{
    interactive = true;
    size = {520.0f, 48.0f};
    m_accessible.role = UIAccessibleRole::KeybindRow;
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

void UIKeybindRow::render(SpriteBatchRenderer& batch,
                          const glm::vec2& parentOffset,
                          int screenWidth, int screenHeight)
{
    if (!visible || theme == nullptr) return;

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    const float keyW    = theme->keybindKeyMinWidth;
    const float keyH    = theme->keybindKeyHeight;
    // Text, and the room kept for it, grow with the interface scale (3D_E-0994).
    const float ui      = theme->defaultTextScale;
    const float clearW  = 60.0f * ui;
    const float gap     = 16.0f * ui;
    const float keyX    = absPos.x + size.x - clearW - gap - keyW;

    // Label (left). One too wide for the room left of the key is drawn smaller.
    if (textRenderer != nullptr && !label.empty())
    {
        const float scale = textRenderer->scaleToFit(label, 0.34f * ui,
                                                     keyX - gap - absPos.x);
        textRenderer->renderText2D(label,
                                    absPos.x,
                                    textRenderer->topForCenteredCaps(
                                        absPos.y + size.y * 0.5f, scale),
                                    scale, theme->textPrimary,
                                    screenWidth, screenHeight);
    }

    // Key-cap (right-of-CLEAR).
    const float keyY = absPos.y + (size.y - keyH) * 0.5f;
    const glm::vec4 keyStroke = (listening || hovered)
        ? theme->accent
        : theme->panelStrokeStrong;
    drawBorder(batch, {keyX, keyY}, {keyW, keyH}, keyStroke,
                theme->panelBorderWidth);

    if (textRenderer != nullptr)
    {
        const std::string keyLabel = listening
            ? std::string(tr("ui.keybind.press_key"))
            : (keyText.empty() ? "—" : keyText);
        const glm::vec3 keyColor = (listening || hovered)
            ? glm::vec3(theme->accent)
            : theme->textPrimary;
        // Approximate centring — TextRenderer doesn't expose measure-text yet.
        const float scale = 0.26f * ui;
        const float approx = static_cast<float>(keyLabel.size()) * 8.0f * scale * 2.5f;
        const float tx = keyX + (keyW - approx) * 0.5f;
        const float ty = textRenderer->topForCenteredCaps(keyY + keyH * 0.5f, scale);
        textRenderer->renderText2D(keyLabel, tx, ty, scale, keyColor,
                                    screenWidth, screenHeight);
    }

    // CLEAR action (right edge).
    if (textRenderer != nullptr)
    {
        const float cx = absPos.x + size.x - clearW;
        const float clearScale = 0.22f * ui;
        const float cy = textRenderer->topForCenteredCaps(absPos.y + size.y * 0.5f, clearScale);
        textRenderer->renderText2D(std::string(tr("ui.keybind.clear")),
                                    cx, cy, clearScale,
                                    theme->textSecondary,
                                    screenWidth, screenHeight);
    }
}

} // namespace Vestige
