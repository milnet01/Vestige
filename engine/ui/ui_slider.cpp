// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_slider.h"
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
}

float UISlider::ratio() const
{
    if (maxValue <= minValue) return 0.0f;
    return std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
}

void UISlider::render(SpriteBatchRenderer& batch,
                      const glm::vec2& parentOffset,
                      int screenWidth, int screenHeight)
{
    if (!visible || theme == nullptr) return;

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);
    const float valueColumnWidth = 72.0f;
    const float trackGap         = 24.0f;

    const float trackY = absPos.y + size.y * 0.5f - theme->sliderTrackHeight * 0.5f;
    const float trackX = absPos.x;
    const float trackW = size.x - valueColumnWidth - trackGap;
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
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d %%", static_cast<int>(value + 0.5f));
            formatted = buf;
        }
        // Right-align by approximate string width — TextRenderer doesn't expose
        // measure-text yet; use a per-char heuristic (mono = ~9 px at scale 0.32).
        const float scale = 0.32f;
        const float approxWidth = static_cast<float>(formatted.size()) * 9.0f * scale * 2.5f;
        const float x = absPos.x + size.x - approxWidth;
        const float y = absPos.y + size.y * 0.5f + 5.0f;
        textRenderer->renderText2D(formatted, x, y, scale,
                                    theme->textSecondary,
                                    screenWidth, screenHeight);
    }
}

} // namespace Vestige
