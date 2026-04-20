// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_button.h"
#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"

namespace Vestige
{

UIButton::UIButton()
{
    interactive = true;
    size = {220.0f, 56.0f};
}

float UIButton::effectiveHeight() const
{
    if (theme == nullptr)
    {
        return small ? 40.0f : 56.0f;
    }
    return small ? theme->buttonHeightSmall : theme->buttonHeight;
}

namespace
{

// Pulls the right background colour for a button's variant + state combo.
// Mirrors styles.css `.btn`, `.btn--primary`, `.btn--ghost`, `.btn--danger`.
glm::vec4 backgroundColor(UIButtonStyle s, UIButtonState st, const UITheme& t)
{
    if (st == UIButtonState::DISABLED) return {0.0f, 0.0f, 0.0f, 0.0f};

    switch (s)
    {
        case UIButtonStyle::PRIMARY:
            // Hover lightens the brass slightly (#d9a84a in the design).
            if (st == UIButtonState::HOVERED) return {0.851f, 0.659f, 0.290f, 1.0f};
            if (st == UIButtonState::PRESSED) return t.accentDim;
            return t.accent;
        case UIButtonStyle::GHOST:
            if (st == UIButtonState::HOVERED) return t.panelBgHover;
            return {0.0f, 0.0f, 0.0f, 0.0f};
        case UIButtonStyle::DANGER:
            // .btn--danger:hover background: rgba(214,106,79,0.08)
            if (st == UIButtonState::HOVERED) return {0.839f, 0.416f, 0.310f, 0.08f};
            if (st == UIButtonState::PRESSED) return t.panelBgPressed;
            return {0.0f, 0.0f, 0.0f, 0.0f};
        case UIButtonStyle::DEFAULT:
        default:
            if (st == UIButtonState::HOVERED) return t.panelBgHover;
            if (st == UIButtonState::PRESSED) return t.panelBgPressed;
            return {0.0f, 0.0f, 0.0f, 0.0f};
    }
}

glm::vec4 borderColor(UIButtonStyle s, UIButtonState st, const UITheme& t)
{
    if (s == UIButtonStyle::PRIMARY)
    {
        if (st == UIButtonState::HOVERED) return {0.851f, 0.659f, 0.290f, 1.0f};
        if (st == UIButtonState::PRESSED) return t.accentDim;
        return t.accent;
    }
    if (s == UIButtonStyle::GHOST)
    {
        if (st == UIButtonState::HOVERED) return t.panelStroke;
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (s == UIButtonStyle::DANGER)
    {
        if (st == UIButtonState::HOVERED)
            return {0.839f, 0.416f, 0.310f, 1.0f};  // textError-coloured border on hover.
        return t.panelStroke;
    }
    // DEFAULT
    if (st == UIButtonState::HOVERED) return t.panelStrokeStrong;
    return t.panelStroke;
}

glm::vec3 textColor(UIButtonStyle s, UIButtonState st, const UITheme& t)
{
    if (st == UIButtonState::DISABLED) return t.textDisabled;
    if (s == UIButtonStyle::PRIMARY)   return t.accentInk;
    if (s == UIButtonStyle::DANGER && st == UIButtonState::HOVERED) return t.textError;
    return t.textPrimary;
}

// Stamp a 1-pixel border around (pos, size) using four 1-px quads.
void drawBorder(SpriteBatchRenderer& batch, const glm::vec2& pos,
                const glm::vec2& sz, const glm::vec4& color, float thickness)
{
    if (color.a <= 0.0f || thickness <= 0.0f) return;
    batch.drawQuad(pos, {sz.x, thickness}, color);                                  // top
    batch.drawQuad({pos.x, pos.y + sz.y - thickness}, {sz.x, thickness}, color);    // bottom
    batch.drawQuad(pos, {thickness, sz.y}, color);                                  // left
    batch.drawQuad({pos.x + sz.x - thickness, pos.y}, {thickness, sz.y}, color);    // right
}

} // namespace

void UIButton::render(SpriteBatchRenderer& batch,
                      const glm::vec2& parentOffset,
                      int screenWidth, int screenHeight)
{
    if (!visible) return;
    if (theme == nullptr) return;

    // Force the configured height — caller's `size.y` is overridden so the
    // button always matches the theme's specified height regardless of how
    // the canvas was authored.
    size.y = effectiveHeight();

    const UIButtonState renderState = disabled ? UIButtonState::DISABLED : state;
    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);

    // 1. Background
    const glm::vec4 bg = backgroundColor(style, renderState, *theme);
    if (bg.a > 0.0f) batch.drawQuad(absPos, size, bg);

    // 2. Border
    drawBorder(batch, absPos, size, borderColor(style, renderState, *theme),
                theme->panelBorderWidth);

    // 3. Hover accent tick (4 px brass strip on the left edge) — DEFAULT + DANGER only.
    if ((style == UIButtonStyle::DEFAULT || style == UIButtonStyle::DANGER)
        && renderState == UIButtonState::HOVERED)
    {
        const float tickHeight = size.y * 0.6f;
        const float tickY = absPos.y + (size.y - tickHeight) * 0.5f;
        batch.drawQuad({absPos.x, tickY},
                        {theme->buttonAccentTickWidth, tickHeight}, theme->accent);
    }

    // 4. Label text
    if (textRenderer != nullptr && !label.empty())
    {
        const glm::vec3 col = textColor(style, renderState, *theme);
        // Vertical centring: TextRenderer::renderText2D draws with the
        // text's baseline near the y coord; centre by approximation
        // (typeButton px size + 25% of size for the cap-height bias).
        const float textScale = 0.4f;  // typeButton (20px) at ~50px font baseline.
        const float pad = theme->buttonPadX;
        const float y = absPos.y + size.y * 0.5f + theme->typeButton * 0.30f;
        textRenderer->renderText2D(label, absPos.x + pad, y, textScale, col,
                                    screenWidth, screenHeight);

        // Right-aligned shortcut key-cap.
        if (shortcut.present && !shortcut.text.empty())
        {
            const float kbdW = 22.0f
                + static_cast<float>(shortcut.text.size()) * 7.0f;
            const float kbdH = 22.0f;
            const glm::vec2 kbdPos{absPos.x + size.x - pad - kbdW,
                                    absPos.y + (size.y - kbdH) * 0.5f};
            drawBorder(batch, kbdPos, {kbdW, kbdH}, theme->panelStroke, 1.0f);
            textRenderer->renderText2D(shortcut.text,
                                        kbdPos.x + 6.0f,
                                        kbdPos.y + kbdH * 0.5f + 4.0f,
                                        0.22f, theme->textSecondary,
                                        screenWidth, screenHeight);
        }
    }
}

} // namespace Vestige
