// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_dropdown.h"
#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"

#include <algorithm>

namespace Vestige
{

UIDropdown::UIDropdown()
{
    interactive = true;
    size = {220.0f, 40.0f};
    m_accessible.role = UIAccessibleRole::Dropdown;
}

namespace
{
const std::string EMPTY_LABEL;

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

const std::string& UIDropdown::currentLabel() const
{
    if (selectedIndex < 0 || static_cast<size_t>(selectedIndex) >= options.size())
    {
        return EMPTY_LABEL;
    }
    return options[static_cast<size_t>(selectedIndex)].label;
}

void UIDropdown::render(SpriteBatchRenderer& batch,
                        const glm::vec2& parentOffset,
                        int screenWidth, int screenHeight)
{
    if (!visible || theme == nullptr) return;

    size.y = theme->dropdownHeight;
    if (size.x < theme->dropdownMinWidth) size.x = theme->dropdownMinWidth;

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);

    // Background — hover brightens.
    if (hovered || open)
    {
        batch.drawQuad(absPos, size, theme->panelBgHover);
    }

    // Border — accent when open, panelStrokeStrong on hover, panelStroke at rest.
    glm::vec4 strokeColor;
    if (open)        strokeColor = theme->accent;
    else if (hovered) strokeColor = theme->panelStrokeStrong;
    else              strokeColor = theme->panelStroke;
    drawBorder(batch, absPos, size, strokeColor, theme->panelBorderWidth);

    // Label + caret.
    if (textRenderer != nullptr)
    {
        const float scale = 0.30f;
        const float pad   = 16.0f;
        const float y     = absPos.y + size.y * 0.5f + 5.0f;
        textRenderer->renderText2D(currentLabel(),
                                    absPos.x + pad, y, scale,
                                    theme->textPrimary,
                                    screenWidth, screenHeight);
        // Caret on the right (mono ▼ / ▲ glyph). FreeType + the engine font
        // may not include arrow glyphs reliably; use a v / ^ ASCII approximation.
        const std::string caret = open ? "^" : "v";
        textRenderer->renderText2D(caret,
                                    absPos.x + size.x - pad - 8.0f, y,
                                    scale, theme->textSecondary,
                                    screenWidth, screenHeight);
    }

    // Popup menu (drawn LAST so it sits over neighbouring elements; the
    // canvas should still order this dropdown after its peers in the
    // element list since the SpriteBatch doesn't reorder draws by z).
    if (open && !options.empty() && textRenderer != nullptr)
    {
        const float itemH       = 36.0f;
        const float menuMaxH    = theme->dropdownMenuMaxHeight;
        const float menuH       = std::min(menuMaxH,
                                            itemH * static_cast<float>(options.size()));
        const glm::vec2 menuPos{absPos.x, absPos.y + size.y + 4.0f};
        const glm::vec2 menuSize{size.x, menuH};

        batch.drawQuad(menuPos, menuSize, theme->bgRaised);
        drawBorder(batch, menuPos, menuSize, theme->panelStrokeStrong,
                    theme->panelBorderWidth);

        for (size_t i = 0; i < options.size(); ++i)
        {
            const float yOff = static_cast<float>(i) * itemH;
            if (yOff + itemH > menuH) break;  // Don't overflow visible area.

            const bool selected = (static_cast<int>(i) == selectedIndex);
            const glm::vec3 col = selected ? glm::vec3(theme->accent) : theme->textPrimary;
            textRenderer->renderText2D(options[i].label,
                                        menuPos.x + 16.0f,
                                        menuPos.y + yOff + itemH * 0.5f + 5.0f,
                                        0.30f, col,
                                        screenWidth, screenHeight);
        }
    }
}

} // namespace Vestige
