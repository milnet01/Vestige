// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file subtitle_renderer.cpp
/// @brief Phase 10.7 slice B2 — SubtitleRenderer implementation.
#include "ui/subtitle_renderer.h"

#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"

#include <algorithm>

namespace Vestige
{

SubtitleStyle styleFor(SubtitleCategory category)
{
    SubtitleStyle style;
    style.plateColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
    switch (category)
    {
        case SubtitleCategory::Dialogue:
            style.textColor    = glm::vec3(1.0f, 1.0f, 1.0f);
            // TLOU2-style yellow for speaker labels — high contrast on
            // the black plate; distinct from white body text so the
            // attribution is legible at a glance.
            style.speakerColor = glm::vec3(1.0f, 0.87f, 0.32f);
            break;
        case SubtitleCategory::Narrator:
            style.textColor    = glm::vec3(1.0f, 1.0f, 1.0f);
            style.speakerColor = style.textColor;
            break;
        case SubtitleCategory::SoundCue:
            // Cyan-grey for non-speech audio cues, matching the Sea of
            // Thieves convention for SFX captions.
            style.textColor    = glm::vec3(0.65f, 0.85f, 0.95f);
            style.speakerColor = style.textColor;
            break;
    }
    return style;
}

namespace
{

/// @brief Composes the string that appears on screen for a subtitle,
///        applying category-specific formatting.
std::string composeText(const Subtitle& s)
{
    switch (s.category)
    {
        case SubtitleCategory::Dialogue:
            if (!s.speaker.empty())
            {
                return s.speaker + ": " + s.text;
            }
            return s.text;
        case SubtitleCategory::SoundCue:
            return "[" + s.text + "]";
        case SubtitleCategory::Narrator:
            break;
    }
    return s.text;
}

} // namespace

std::vector<SubtitleLineLayout> computeSubtitleLayout(
    const SubtitleQueue& queue,
    const SubtitleLayoutParams& params,
    const std::function<float(const std::string&)>& measureTextWidthPx)
{
    std::vector<SubtitleLineLayout> out;

    const std::vector<ActiveSubtitle>& active = queue.activeSubtitles();
    if (active.empty())
    {
        return out;
    }

    // basePx = 46 × (viewport_h / 1080) — GAG 1080p minimum, scaled
    // linearly with resolution. Preset multiplier on top.
    const float resolutionScale =
        static_cast<float>(params.screenHeight) / params.referenceHeight;
    const float basePx =
        params.baseReferencePx * resolutionScale *
        subtitleScaleFactorOf(queue.sizePreset());

    // Convert target pixel height to `TextRenderer::renderText2D`
    // scale — renderText2D multiplies the font atlas pixel size by
    // `scale`, so for a target of basePx pixels the scale is
    // basePx / fontPixelSize.
    const float fontPixelSize = static_cast<float>(std::max(1, params.fontPixelSize));
    const float textScale = basePx / fontPixelSize;

    // Estimate rendered line height. The font's own ascender+descender
    // is atlas-size-dependent; use basePx as a proxy (safe overestimate
    // that keeps plates non-overlapping for every loaded font).
    const float lineHeightPx = basePx + 2.0f * params.platePaddingY;

    // Stack plates from the bottom upward. activeSubtitles() returns
    // entries in enqueue order (oldest first). Convention: newest
    // caption sits at the bottom-most slot; older captions stack
    // above — matches TLOU2 / Sea of Thieves reading order.
    const std::size_t count = active.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        // The bottom-most row is activeSubtitles[count-1]; it sits
        // `bottomMarginFrac × screenHeight` above the bottom edge.
        // Each earlier row stacks one `lineHeightPx + lineSpacingPx`
        // higher.
        const std::size_t rowFromBottom = (count - 1) - i;
        const Subtitle& sub = active[i].subtitle;
        const SubtitleStyle style = styleFor(sub.category);

        SubtitleLineLayout line;
        line.fullText   = composeText(sub);
        line.category   = sub.category;
        line.textColor  = style.textColor;
        line.plateColor = style.plateColor;
        line.textScale  = textScale;

        // Measure text at base scale (scale=1 → fontPixelSize). Scale
        // with the same factor applied at draw time so plate width
        // matches rendered width.
        const float rawWidthPx  = measureTextWidthPx
            ? measureTextWidthPx(line.fullText)
            : 0.0f;
        const float textWidthPx = rawWidthPx * textScale;

        line.plateSize.x = textWidthPx + 2.0f * params.platePaddingX;
        line.plateSize.y = lineHeightPx;

        // Center horizontally.
        line.platePos.x = 0.5f * (static_cast<float>(params.screenWidth)
                                  - line.plateSize.x);

        // Y: distance from top (renderText2D uses top-left origin).
        // bottomY = screenHeight × (1 - bottomMarginFrac) = the bottom
        // edge of the bottom-most plate. The bottom-most plate's
        // top-left is therefore bottomY - lineHeightPx; each row above
        // adds one (lineHeightPx + lineSpacingPx) step.
        const float bottomY =
            static_cast<float>(params.screenHeight) *
            (1.0f - params.bottomMarginFrac);
        line.platePos.y = bottomY
            - lineHeightPx
            - static_cast<float>(rowFromBottom) *
              (lineHeightPx + params.lineSpacingPx);

        // Text baseline: inside the plate, offset by padding.
        line.textBaseline.x = line.platePos.x + params.platePaddingX;
        line.textBaseline.y = line.platePos.y + params.platePaddingY;

        out.push_back(line);
    }

    return out;
}

void renderSubtitles(const std::vector<SubtitleLineLayout>& lines,
                     SpriteBatchRenderer& batch,
                     TextRenderer& textRenderer,
                     int screenWidth,
                     int screenHeight)
{
    if (lines.empty())
    {
        return;
    }

    // Plates first, through the already-open sprite batch.
    for (const auto& line : lines)
    {
        batch.drawQuad(line.platePos, line.plateSize, line.plateColor);
    }

    // Flush plate batch so text draws land on top of the plates.
    // SpriteBatchRenderer::end()→flush() is idempotent: the caller's
    // eventual outer `batch.end()` is a no-op after this one, so no
    // re-begin is needed here. If a future caller wants to continue
    // pushing quads after `renderSubtitles` returns, it should call
    // `batch.begin()` itself before doing so.
    batch.end();

    // Then text, which uses its own shader + VAO and manages its own
    // blend state internally.
    for (const auto& line : lines)
    {
        textRenderer.renderText2D(
            line.fullText,
            line.textBaseline.x,
            line.textBaseline.y,
            line.textScale,
            line.textColor,
            screenWidth,
            screenHeight);
    }
}

} // namespace Vestige
