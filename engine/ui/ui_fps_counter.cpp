// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_fps_counter.h"
#include "renderer/text_renderer.h"

#include <algorithm>
#include <cstdio>

namespace Vestige
{

void UIFpsCounter::tick(float deltaSeconds)
{
    if (deltaSeconds <= 0.0f) return;
    const float instantFps = 1.0f / deltaSeconds;

    // Exponential moving average; first sample seeds the average outright so
    // the very first frame doesn't display 0 FPS.
    if (m_smoothedFps <= 0.0f)
    {
        m_smoothedFps = instantFps;
        return;
    }
    const float w = std::clamp(smoothing, 0.0f, 1.0f);
    m_smoothedFps = m_smoothedFps * (1.0f - w) + instantFps * w;
}

void UIFpsCounter::render(SpriteBatchRenderer& /*batch*/,
                           const glm::vec2& parentOffset,
                           int screenWidth, int screenHeight)
{
    if (!visible || !textRenderer) return;

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f FPS", static_cast<double>(m_smoothedFps));

    textRenderer->renderText2D(buf, absPos.x, absPos.y, scale, color,
                                screenWidth, screenHeight);
}

} // namespace Vestige
