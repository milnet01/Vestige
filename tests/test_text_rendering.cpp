// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_text_rendering.cpp
/// @brief Tests for text rendering subsystem (Font, GlyphInfo, TextRenderer).
#include <gtest/gtest.h>
#include "renderer/font.h"
#include "renderer/font_stack.h"
#include "renderer/text_renderer.h"
#include "utils/utf8.h"

#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include <memory>

#include <chrono>
#include <string>

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
    const GlyphInfo& glyph = font.getGlyph(static_cast<uint32_t>('A'));
    // Should be the default-constructed fallback
    EXPECT_EQ(glyph.advance, 0);
}

// =============================================================================
// Phase 10 Localization L1 — codepoint Font API (headless parts)
// =============================================================================

TEST(TextRendering, GlyphInfoEqualityIsFieldwise)
{
    GlyphInfo a;
    GlyphInfo b;
    EXPECT_TRUE(a == b);   // two default glyphs compare equal
    EXPECT_FALSE(a != b);

    b.advance = 1;
    EXPECT_FALSE(a == b);  // a single differing field breaks equality
    EXPECT_TRUE(a != b);

    GlyphInfo c;
    c.bearing = glm::ivec2(3, -2);
    EXPECT_NE(a, c);
}

TEST(TextRendering, HasGlyphFalseOnUnloadedFont)
{
    Font font;
    // No glyphs rasterised yet — hasGlyph is false for everything, including
    // the char-shim path. getGlyph still returns the fallback (no crash).
    EXPECT_FALSE(font.hasGlyph(static_cast<uint32_t>('A')));
    EXPECT_FALSE(font.hasGlyph(0x05D0));  // Hebrew aleph
}

// Ad-hoc timing probe (design § 1 / § 8: each pre-L6 slice records a
// lightweight baseline for the work it adds; L6 test 23 supersedes it as the
// pinned HUD-pass gate). Non-gating — it asserts only correctness of the walk,
// and records the decode time as a gtest property rather than failing a budget.
TEST(TextRendering, Utf8DecodeWalkProbe_L1)
{
    // ~800-codepoint mixed-script workload, mirroring the § 9 HUD estimate.
    std::string sample;
    for (int i = 0; i < 100; ++i)
    {
        sample += "Holy ";          // Latin
        sample += "\xCE\x91\xCF\x81";  // Greek Α ρ
        sample += "\xD7\xA9\xD7\x9C";  // Hebrew ש ל
    }

    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<uint32_t> cps = utf8::decode(sample);
    const auto t1 = std::chrono::steady_clock::now();

    // 100 reps * (5 ASCII "Holy " + 2 Greek + 2 Hebrew) = 900 codepoints.
    EXPECT_EQ(cps.size(), 900u);

    const double us =
        std::chrono::duration<double, std::micro>(t1 - t0).count();
    RecordProperty("utf8_decode_us_900cp", static_cast<int>(us));
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
    // L2 — a NON-empty string before init must also be safe (empty font stack
    // would otherwise deref a null Hit::glyph). Pins the m_initialized guard.
    EXPECT_FLOAT_EQ(renderer.measureTextWidth("Hello"), 0.0f);
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
// Phase 10.9 Slice 18 Ts1 cleanup: dropped the previous "documented
// behavior" trio (LineHeightIsPositive, AscenderGreaterThanDescender,
// ASCIIRangeIs95Printable) — each was a tautology on a local literal,
// not a SUT assertion. Font-metric properties are pinned at engine
// launch when a GL context is available.
//
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

// =============================================================================
// Phase 10 Localization L1 — Font codepoint API under a real GL context.
// Loading a TTF needs a GL context for the atlas texture, so these run under
// GLTestFixture (skips when no display — see gl_test_fixture.h). Mirrors the
// design's § 8 test 5 (Font.AsciiBackwardCompat) + the ranges-API exercise.
// =============================================================================

namespace
{
std::string arimoPath()
{
    return std::string(VESTIGE_FONT_DIR) + "/arimo.ttf";
}

std::string frankRuhlPath()
{
    return std::string(VESTIGE_FONT_DIR) + "/frank_ruhl_libre.ttf";
}

// Asset root = parent of the fonts dir; TextRenderer::initialize appends
// "/shaders/text.{vert,frag}.glsl" to this.
std::string assetRoot()
{
    return std::string(VESTIGE_FONT_DIR) + "/..";
}

// Count non-fallback glyphs a font carries within an inclusive codepoint range.
int countGlyphsInRange(const Font& font, uint32_t first, uint32_t last)
{
    int n = 0;
    for (uint32_t cp = first; cp <= last; ++cp)
    {
        if (font.hasGlyph(cp)) ++n;
    }
    return n;
}
}  // namespace

class FontGLTest : public ::Vestige::Test::GLTestFixture
{
};

// Test 5 — the default (two-arg) load is byte-identical to an explicit
// ASCII_RANGE load, and 'A' is a real glyph. Pins the L1 back-compat claim:
// the codepoint migration did not change what the ASCII path produces.
TEST_F(FontGLTest, AsciiBackwardCompat)
{
    Font defaultLoad;
    ASSERT_TRUE(defaultLoad.loadFromFile(arimoPath(), 48));
    ASSERT_TRUE(defaultLoad.isLoaded());

    Font explicitAscii;
    ASSERT_TRUE(explicitAscii.loadFromFile(arimoPath(), 48, ASCII_RANGE));

    // Default range == ASCII_RANGE: same glyph count and same 'A' geometry.
    EXPECT_EQ(defaultLoad.getGlyphCount(), explicitAscii.getGlyphCount());
    EXPECT_EQ(defaultLoad.getGlyph(static_cast<uint32_t>('A')),
              explicitAscii.getGlyph(static_cast<uint32_t>('A')));

    // 'A' is a real (non-fallback) glyph with sane metrics.
    EXPECT_TRUE(defaultLoad.hasGlyph(static_cast<uint32_t>('A')));
    const GlyphInfo& a = defaultLoad.getGlyph(static_cast<uint32_t>('A'));
    EXPECT_GT(a.advance, 0);
    EXPECT_GT(a.size.x, 0);
}

// Range scoping — an ASCII-only load must NOT carry Hebrew/Greek glyphs.
TEST_F(FontGLTest, AsciiLoadExcludesNonAsciiCodepoints)
{
    Font font;
    ASSERT_TRUE(font.loadFromFile(arimoPath(), 48, ASCII_RANGE));
    EXPECT_FALSE(font.hasGlyph(0x05D0));  // Hebrew aleph — not requested
    EXPECT_FALSE(font.hasGlyph(0x0391));  // Greek capital alpha — not requested
}

// The new `ranges` parameter loads beyond ASCII — Greek from Arimo.
// (Full per-script coverage counts are pinned by the L2 test 6/7 once the
// dedicated Hebrew face is in tree; here we only prove the L1 API works.)
TEST_F(FontGLTest, LoadsGreekRange)
{
    Font font;
    ASSERT_TRUE(font.loadFromFile(arimoPath(), 48, GREEK_RANGES));
    EXPECT_TRUE(font.hasGlyph(0x0391));   // Greek capital alpha
    EXPECT_TRUE(font.hasGlyph(0x03C9));   // Greek small omega
    EXPECT_FALSE(font.hasGlyph(static_cast<uint32_t>('A')));  // ASCII not requested
    EXPECT_GE(font.getGlyphCount(), 100u);
}

// =============================================================================
// Phase 10 Localization L2 — FontStack + bundled Frank Ruhl Libre Hebrew face
// =============================================================================

// Test 6 — the bundled Hebrew face carries the full Hebrew alphabet, and Arimo
// carries the Greek block. Pins the per-script glyph coverage of the default
// stack's two fonts.
TEST_F(FontGLTest, LoadsHebrewRangeFromFrankRuhlLibre)
{
    Font hebrew;
    ASSERT_TRUE(hebrew.loadFromFile(frankRuhlPath(), 48, HEBREW_RANGE));
    // 22 base letters + 5 final forms = 27 floor (forces the finals present).
    EXPECT_GE(countGlyphsInRange(hebrew, 0x0590, 0x05FF), 27);
    EXPECT_TRUE(hebrew.hasGlyph(0x05D0));  // aleph
    EXPECT_TRUE(hebrew.hasGlyph(0x05EA));  // tav

    Font greek;
    ASSERT_TRUE(greek.loadFromFile(arimoPath(), 48, GREEK_RANGES));
    EXPECT_GE(countGlyphsInRange(greek, 0x0370, 0x1FFF), 120);
}

// Test 7 — the default 2-font stack routes each script to its own font, each
// returning a real (non-fallback) glyph from a distinct Font*.
TEST_F(FontGLTest, FontStackRoutesLatinAndHebrewToDistinctFonts)
{
    auto latinGreek = std::make_shared<Font>();
    ASSERT_TRUE(latinGreek->loadFromFile(arimoPath(), 48, ASCII_RANGE));
    auto hebrew = std::make_shared<Font>();
    ASSERT_TRUE(hebrew->loadFromFile(frankRuhlPath(), 48, HEBREW_RANGE));

    FontStack stack;
    stack.addFont(latinGreek);
    stack.addFont(hebrew);

    FontStack::Hit latinHit = stack.lookup(static_cast<uint32_t>('A'));
    FontStack::Hit hebrewHit = stack.lookup(0x05D0);  // aleph

    EXPECT_EQ(latinHit.font, latinGreek.get());
    EXPECT_EQ(hebrewHit.font, hebrew.get());
    EXPECT_NE(latinHit.font, hebrewHit.font);
    // Both are real glyphs (non-zero advance, not the fallback box).
    EXPECT_GT(latinHit.glyph->advance, 0);
    EXPECT_GT(hebrewHit.glyph->advance, 0);
}

// Test 8 — an unmapped codepoint returns the first font's fallback '?' glyph,
// with a non-null font, never a crash (pins § 6 deferral 3 + the miss contract).
TEST_F(FontGLTest, FontStackMissingCodepointReturnsFallback)
{
    auto latinGreek = std::make_shared<Font>();
    ASSERT_TRUE(latinGreek->loadFromFile(arimoPath(), 48, ASCII_RANGE));
    auto hebrew = std::make_shared<Font>();
    ASSERT_TRUE(hebrew->loadFromFile(frankRuhlPath(), 48, HEBREW_RANGE));

    FontStack stack;
    stack.addFont(latinGreek);
    stack.addFont(hebrew);

    FontStack::Hit miss = stack.lookup(0x4E2D);  // Chinese — covered by neither

    ASSERT_NE(miss.font, nullptr);
    EXPECT_EQ(miss.font, latinGreek.get());  // first font in the stack
    ASSERT_NE(miss.glyph, nullptr);
    // The fallback is a copy of the '?' glyph (font.cpp populates m_fallbackGlyph
    // from '?'), so getGlyph on a miss equals getGlyph('?').
    EXPECT_EQ(*miss.glyph, latinGreek->getGlyph(static_cast<uint32_t>('?')));
}

// Test 9 — the TextRenderer's one-element MRU cache short-circuits the stack
// walk on a pure-script string: FontStack::lookup is hit exactly once (the
// first glyph), zero additional times for the remaining same-font glyphs.
TEST_F(FontGLTest, MruCacheSkipsStackWalk)
{
    TextRenderer tr;
    ASSERT_TRUE(tr.initialize(arimoPath(), assetRoot(), 48));

    const std::string latin(1000, 'A');  // 1000-glyph pure-Latin string
    tr.getFontStack().resetLookupCalls();
    {
        // The first glDrawArrays makes the llvmpipe software rasterizer (CI's
        // headless GL) JIT-compile pipe-state it never frees — a known
        // third-party process-lifetime allocation, not a Vestige leak. Bracket
        // the draw so LeakSanitizer doesn't flag it (same idiom as
        // shader_parity_helpers.cpp; see tests/lsan_guard.h + engine
        // CMakeLists.txt § "Debug sanitizers").
        Vestige::Test::ScopedLeakCheckDisable noLeakTracking;
        tr.renderText2D(latin, 10.0f, 10.0f, 1.0f, glm::vec3(1.0f), 1920, 1080);
    }

    // First glyph warms the MRU (1 lookup); the next 999 reuse the cached font.
    EXPECT_EQ(tr.getFontStack().lookupCalls(), 1u);
}
