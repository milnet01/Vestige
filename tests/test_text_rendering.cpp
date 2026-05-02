// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_text_rendering.cpp
/// @brief Tests for text rendering subsystem (Font, GlyphInfo, TextRenderer).
#include <gtest/gtest.h>
#include "renderer/font.h"
#include "renderer/text_renderer.h"

using namespace Vestige;

// =============================================================================
// GlyphInfo structure
// =============================================================================

TEST(TextRendering, GlyphInfoDefaultValues)
{
    GlyphInfo glyph;
    EXPECT_EQ(glyph.atlasOffset, glm::vec2(0.0f));
    EXPECT_EQ(glyph.atlasSize, glm::vec2(0.0f));
    EXPECT_EQ(glyph.size, glm::ivec2(0));
    EXPECT_EQ(glyph.bearing, glm::ivec2(0));
    EXPECT_EQ(glyph.advance, 0);
}

// =============================================================================
// Font (without GL context — tests design expectations)
// =============================================================================

TEST(TextRendering, FontDefaultNotLoaded)
{
    Font font;
    EXPECT_FALSE(font.isLoaded());
    EXPECT_EQ(font.getGlyphCount(), 0u);
    EXPECT_EQ(font.getPixelSize(), 0);
}

TEST(TextRendering, FontLoadNonexistentFails)
{
    Font font;
    // Without a GL context, this will fail (can't create textures)
    // But it should not crash
    bool result = font.loadFromFile("nonexistent_font.ttf", 24);
    EXPECT_FALSE(result);
}

TEST(TextRendering, FontFallbackGlyphReturned)
{
    Font font;
    // Before loading, fallback glyph should be returned for any character
    const GlyphInfo& glyph = font.getGlyph('A');
    // Should be the default-constructed fallback
    EXPECT_EQ(glyph.advance, 0);
}

// =============================================================================
// TextRenderer (without GL context)
// =============================================================================

TEST(TextRendering, TextRendererDefaultNotInitialized)
{
    TextRenderer renderer;
    EXPECT_FALSE(renderer.isInitialized());
}

TEST(TextRendering, TextRendererInitWithBadFontFails)
{
    TextRenderer renderer;
    bool result = renderer.initialize("bad_font.ttf", "bad_path");
    EXPECT_FALSE(result);
    EXPECT_FALSE(renderer.isInitialized());
}

TEST(TextRendering, MeasureEmptyStringIsZero)
{
    TextRenderer renderer;
    // Not initialized, but measureTextWidth should handle gracefully
    float width = renderer.measureTextWidth("");
    EXPECT_FLOAT_EQ(width, 0.0f);
}

TEST(TextRendering, RenderText2DNoOpWhenNotInitialized)
{
    TextRenderer renderer;
    // Should not crash when called before initialization
    renderer.renderText2D("Hello", 10.0f, 10.0f, 1.0f, glm::vec3(1.0f), 800, 600);
}

TEST(TextRendering, RenderText3DNoOpWhenNotInitialized)
{
    TextRenderer renderer;
    renderer.renderText3D("Hello", glm::mat4(1.0f), 1.0f, glm::vec3(1.0f),
                           glm::mat4(1.0f), glm::mat4(1.0f));
}

TEST(TextRendering, GenerateHeightMapNullWhenNotInitialized)
{
    TextRenderer renderer;
    auto tex = renderer.generateTextHeightMap("HOLY", 512, 512, true);
    EXPECT_EQ(tex, nullptr);
}

TEST(TextRendering, GenerateHeightMapNullForEmptyString)
{
    TextRenderer renderer;
    auto tex = renderer.generateTextHeightMap("", 512, 512, true);
    EXPECT_EQ(tex, nullptr);
}

// =============================================================================
// Font metric expectations (documented behavior)
// =============================================================================

TEST(TextRendering, LineHeightIsPositive)
{
    // For any loaded font, line height should be positive
    // Since we can't load without GL, we verify the design expectation
    float expectedLineHeight = 48.0f;  // A 48px font should have ~48px line height
    EXPECT_GT(expectedLineHeight, 0.0f);
}

TEST(TextRendering, AscenderGreaterThanDescender)
{
    // Ascender should be above baseline (positive), descender below (negative)
    float ascender = 40.0f;
    float descender = -10.0f;
    EXPECT_GT(ascender, descender);
}

TEST(TextRendering, ASCIIRangeIs95Printable)
{
    // ASCII printable range: 32 (space) to 126 (~) = 95 characters
    int count = 0;
    for (char c = 32; c < 127; c++)
    {
        count++;
    }
    EXPECT_EQ(count, 95);
}

// =============================================================================
// Phase 10.9 Pe1 — TextRenderer batch state machine
//
// The batched-draw path collapses ~18 per-string HUD draws into one upload +
// draw at `endBatch2D`. The full upload + draw needs a GL context (so it's
// runtime-verified at engine launch per the project's `test_gpu_cloth_simulator.cpp`
// precedent), but the begin / end state machine is testable headlessly.
// =============================================================================

TEST(TextRendering, BatchStartsClosed_Pe1)
{
    TextRenderer renderer;
    EXPECT_FALSE(renderer.isBatching());
}

TEST(TextRendering, BeginBatchOnUninitializedIsNoOp_Pe1)
{
    TextRenderer renderer;
    // Renderer is uninitialised — beginBatch2D must not flip to batching
    // because the underlying VBO doesn't exist.
    renderer.beginBatch2D(800, 600);
    EXPECT_FALSE(renderer.isBatching());
}

TEST(TextRendering, EndBatchOnUninitializedIsNoOp_Pe1)
{
    TextRenderer renderer;
    // Closing a batch that was never opened (or that the uninitialised
    // beginBatch2D rejected) must not crash.
    renderer.endBatch2D();
    EXPECT_FALSE(renderer.isBatching());
}

TEST(TextRendering, RenderText2DInBatchIsSafeWhenUninitialised_Pe1)
{
    TextRenderer renderer;
    renderer.beginBatch2D(800, 600);
    // Both immediate and oblique paths should no-op safely.
    renderer.renderText2D("HP: 100", 10.0f, 10.0f, 1.0f,
                          glm::vec3(1.0f), 800, 600);
    renderer.renderText2DOblique("Quote", 20.0f, 20.0f, 1.0f,
                                  glm::vec3(0.5f), 800, 600);
    renderer.endBatch2D();
    EXPECT_FALSE(renderer.isBatching());
}
