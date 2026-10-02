// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_slider.h"
#include "localization/localization_service.h"
#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"

#include <algorithm>
#include <cstdio>

namespace Vestige
{

UISlider::UISlider()
{
    interactive = true;
    size = {280.0f, 44.0f};
    m_accessible.role = UIAccessibleRole::Slider;
}

float UISlider::ratio() const
{
    if (maxValue <= minValue) return 0.0f;
    return std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
}

namespace
{
constexpr float kValueColumnWidth = 72.0f;
constexpr float kTrackGap         = 24.0f;
}

float UISlider::trackWidth() const
{
    return std::max(size.x - kValueColumnWidth - kTrackGap, 1.0f);
}

bool UISlider::adjust(int direction)
{
    const float step = (keyStep > 0.0f) ? keyStep : (maxValue - minValue) / 20.0f;
    const float sign = (direction < 0) ? -1.0f : 1.0f;
    const float next = std::clamp(value + sign * step, minValue, maxValue);
    if (next != value)
    {
        value = next;
        onValueChanged.emit(value);
    }
    return true;
}

void UISlider::pointerPress(const glm::vec2& local)
{
    pointerDrag(local);
}

void UISlider::pointerDrag(const glm::vec2& local)
{
    const float r = std::clamp(local.x / trackWidth(), 0.0f, 1.0f);
    const float next = minValue + r * (maxValue - minValue);
    if (next != value)
    {
        value = next;
        onValueChanged.emit(value);
    }
}

void UISlider::render(SpriteBatchRenderer& batch,
                      const glm::vec2& parentOffset,
                      int screenWidth, int screenHeight)
{
    if (!visible || theme == nullptr) return;

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    const float trackY = absPos.y + size.y * 0.5f - theme->sliderTrackHeight * 0.5f;
    const float trackX = absPos.x;
    const float trackW = trackWidth();
    const float r = ratio();

    // Track background.
    batch.drawQuad({trackX, trackY},
                    {trackW, theme->sliderTrackHeight},
                    theme->progressBarEmpty);

    // Tick marks (1-px verticals across the track).
    if (ticks > 0)
    {
        for (int i = 0; i <= ticks; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(ticks);
            batch.drawQuad({trackX + t * trackW - 0.5f, trackY},
                            {1.0f, theme->sliderTrackHeight},
                            theme->rule);
        }
    }

    // Track fill.
    if (r > 0.0f)
    {
        batch.drawQuad({trackX, trackY},
                        {trackW * r, theme->sliderTrackHeight},
                        theme->accent);
    }

    // Thumb (16x16, accent ring around bone-coloured fill).
    const float thumbCx = trackX + trackW * r;
    const float thumbCy = absPos.y + size.y * 0.5f;
    const float ts      = theme->sliderThumbSize;
    const float tb      = theme->sliderThumbBorder;
    // Outer (border) quad.
    batch.drawQuad({thumbCx - ts * 0.5f, thumbCy - ts * 0.5f},
                    {ts, ts}, theme->accent);
    // Inner fill (textPrimary).
    batch.drawQuad({thumbCx - ts * 0.5f + tb, thumbCy - ts * 0.5f + tb},
                    {ts - 2.0f * tb, ts - 2.0f * tb},
                    glm::vec4(theme->textPrimary, 1.0f));

    // Value readout (mono, right-aligned, tabular).
    if (textRenderer != nullptr)
    {
        std::string formatted;
        if (formatter)
        {
            formatted = formatter(value);
        }
        else
        {
            formatted = composePercentText(static_cast<int>(value + 0.5f));
        }
        // Right-align by approximate string width — TextRenderer doesn't expose
        // measure-text yet; use a per-char heuristic (mono = ~9 px at scale 0.32).
        const float scale = 0.32f;
        const float approxWidth = static_cast<float>(formatted.size()) * 9.0f * scale * 2.5f;
        const float x = absPos.x + size.x - approxWidth;
        const float y = textRenderer->topForCenteredCaps(absPos.y + size.y * 0.5f, scale);
        textRenderer->renderText2D(formatted, x, y, scale,
                                    theme->textSecondary,
                                    screenWidth, screenHeight);
    }
}

std::string UISlider::composePercentText(int percent)
{
    return trf("ui.slider.percent", {{"value", std::to_string(percent)}});
}

} // namespace Vestige
