// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_dropdown.h"
#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"

#include <algorithm>
#include <cstddef>

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
constexpr float kItemHeight = 36.0f;  ///< Height of one row in the open list.
constexpr float kMenuGap    = 4.0f;   ///< Gap between the box and the list.
}

float UIDropdown::visibleMenuHeight() const
{
    const float full = kItemHeight * static_cast<float>(options.size());
    return theme != nullptr ? std::min(theme->dropdownMenuMaxHeight, full) : full;
}

int UIDropdown::optionAt(const glm::vec2& local) const
{
    const float top = size.y + kMenuGap;
    if (local.x < 0.0f || local.x > size.x || local.y < top)
    {
        return -1;
    }
    const auto row = static_cast<std::size_t>((local.y - top) / kItemHeight);
    const float rowBottom = static_cast<float>(row + 1) * kItemHeight;
    if (row >= options.size() || rowBottom > visibleMenuHeight())
    {
        return -1;
    }
    return static_cast<int>(row);
}

void UIDropdown::choose(int index)
{
    open = false;
    if (index < 0 || static_cast<std::size_t>(index) >= options.size())
    {
        return;
    }
    selectedIndex = index;
    onSelectionChanged.emit(index);
}

void UIDropdown::activate()
{
    open = !open;
}

void UIDropdown::pointerPress(const glm::vec2& local)
{
    if (!open)
    {
        open = true;
        return;
    }
    // Open: a press on a row picks it; anywhere else closes the list.
    choose(optionAt(local));
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
        const float y     = textRenderer->topForCenteredCaps(absPos.y + size.y * 0.5f, scale);
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
        const float itemH       = kItemHeight;
        const float menuH       = visibleMenuHeight();
        const glm::vec2 menuPos{absPos.x, absPos.y + size.y + kMenuGap};
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
                                        textRenderer->topForCenteredCaps(
                                            menuPos.y + yOff + itemH * 0.5f, 0.30f),
                                        0.30f, col,
                                        screenWidth, screenHeight);
        }
    }
}

} // namespace Vestige
