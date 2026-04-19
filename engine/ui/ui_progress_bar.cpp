// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/ui_progress_bar.h"
#include "ui/sprite_batch_renderer.h"

#include <algorithm>

namespace Vestige
{

float UIProgressBar::getRatio() const
{
    if (maxValue <= 0.0f) return 0.0f;
    return std::clamp(value / maxValue, 0.0f, 1.0f);
}

void UIProgressBar::render(SpriteBatchRenderer& batch,
                            const glm::vec2& parentOffset,
                            int screenWidth, int screenHeight)
{
    if (!visible) return;

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);

    // Background (empty bar) — full width.
    batch.drawQuad(absPos, size, emptyColor);

    // Foreground (fill) — width = size.x * ratio. Skip the draw call entirely
    // when ratio == 0 to keep the batch tidy.
    const float ratio = getRatio();
    if (ratio > 0.0f)
    {
        const glm::vec2 fillSize{size.x * ratio, size.y};
        batch.drawQuad(absPos, fillSize, fillColor);
    }
}

} // namespace Vestige
