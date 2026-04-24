// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file subtitle_renderer.h
/// @brief Phase 10.7 slice B2 — renders `SubtitleQueue::activeSubtitles()`
///        through the existing `SpriteBatchRenderer` (plate quads) and
///        `TextRenderer` (glyph runs). Pure-function layout + thin GL
///        dispatch so the layout math is unit-testable without a GL
///        context.
///
/// Design references: `docs/PHASE10_7_DESIGN.md` §4.2, Game
/// Accessibility Guidelines, FCC 2024 caption-display rule.
#pragma once

#include "ui/subtitle.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Vestige
{

class SpriteBatchRenderer;
class TextRenderer;

/// @brief Per-category styling baked into the layout output so the
///        renderer does not branch on `SubtitleCategory` at draw time.
struct SubtitleStyle
{
    glm::vec3 textColor   = glm::vec3(1.0f); ///< Body text (RGB).
    glm::vec3 speakerColor= glm::vec3(1.0f); ///< Speaker-label colour (dialogue only).
    glm::vec4 plateColor  = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f); ///< 50 % black plate.
};

/// @brief Returns the default style for a category.
///
/// Dialogue — yellow speaker label, white body text.
/// Narrator — plain white body text (no speaker label drawn).
/// SoundCue — cyan-grey body, text pre-bracketed by the layout pass.
SubtitleStyle styleFor(SubtitleCategory category);

/// @brief Pure layout parameters — everything the layout pass needs.
///
/// Kept as a small struct so unit tests can build fixed inputs.
struct SubtitleLayoutParams
{
    int   screenWidth  = 1920;   ///< Viewport width in pixels.
    int   screenHeight = 1080;   ///< Viewport height in pixels.
    int   fontPixelSize= 48;     ///< Pixel size the font atlas was loaded at.
    float baseReferencePx = 46.0f; ///< GAG 1080p minimum. Scaled with resolution.
    float referenceHeight = 1080.0f; ///< Reference viewport height for basePx scaling.
    float bottomMarginFrac = 0.12f;  ///< Distance from bottom of viewport, as fraction of height.
    float platePaddingX = 8.0f;  ///< Horizontal padding (pixels) inside the plate.
    float platePaddingY = 4.0f;  ///< Vertical padding (pixels) inside the plate.
    float lineSpacingPx = 4.0f;  ///< Gap between stacked plates.
};

/// @brief One laid-out subtitle line. Every field is pre-computed so
///        the GL pass only needs to issue draw calls.
struct SubtitleLineLayout
{
    std::string fullText;          ///< Composed string (pre-wrap) for back-compat.
    /// Wrapped caption rows (Phase 10.9 P1). `wrappedLines[0]` is the
    /// topmost row; `wrappedLines.back()` is the bottommost. `fullText`
    /// is preserved as the pre-wrap composition so existing callers
    /// (and tests) that check composition can keep doing so without
    /// touching wrap-specific concerns.
    std::vector<std::string> wrappedLines;
    glm::vec2   platePos{0.0f};    ///< Top-left of the background plate (px).
    glm::vec2   plateSize{0.0f};   ///< Width × height of the background plate (px).
    glm::vec4   plateColor{0.0f};  ///< RGBA of the plate.
    glm::vec2   textBaseline{0.0f};///< (x, y) passed to `renderText2D` for line 0.
    float       lineStepPx = 0.0f; ///< Y increment between wrapped rows (px).
    float       textScale = 1.0f;  ///< Multiplier on font atlas pixel size.
    glm::vec3   textColor{1.0f};   ///< Body text colour.
    SubtitleCategory category = SubtitleCategory::Dialogue;
};

/// @brief Pure layout pass. Produces one `SubtitleLineLayout` per
///        caption returned by `queue.activeSubtitles()`, stacked from
///        the bottom of the viewport upward (newest caption at bottom).
///
/// No GL dependencies — callable from tests.
///
/// `measureTextWidthPx` is a callable that returns the rendered width
/// of a string at the font's base pixel size (i.e. scale = 1). The
/// production caller passes `TextRenderer::measureTextWidth`; tests
/// pass a deterministic stub. Returning 0 is safe (plate will shrink
/// to padding only).
std::vector<SubtitleLineLayout> computeSubtitleLayout(
    const SubtitleQueue& queue,
    const SubtitleLayoutParams& params,
    const std::function<float(const std::string&)>& measureTextWidthPx);

/// @brief Renders a laid-out caption set to the current GL context.
///
/// Caller is responsible for:
///   - Having already set up a 2D overlay pass (depth-test off,
///     blend src-alpha / one-minus-src-alpha). `UISystem::renderUI`
///     does this.
///   - Having called `batch.begin()` before `renderSubtitles` and
///     `batch.end()` after. This keeps the sprite-batch flush under
///     the caller's control.
///
/// Draws plates through the sprite batch, then flushes and draws
/// text through `TextRenderer::renderText2D`. Plates and text end
/// up on separate draw calls, but that is fine at < 10 plates per
/// frame.
void renderSubtitles(const std::vector<SubtitleLineLayout>& lines,
                     SpriteBatchRenderer& batch,
                     TextRenderer& textRenderer,
                     int screenWidth,
                     int screenHeight);

} // namespace Vestige
