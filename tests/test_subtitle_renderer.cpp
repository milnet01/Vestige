// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_subtitle_renderer.cpp
/// @brief Phase 10.7 slice B2 — pure-function layout coverage for
///        the subtitle HUD pass. No GL state touched.

#include <gtest/gtest.h>

#include "ui/subtitle.h"
#include "ui/subtitle_renderer.h"

using namespace Vestige;

namespace
{

Subtitle makeLine(const std::string& text,
                  float duration = 3.0f,
                  SubtitleCategory cat = SubtitleCategory::Dialogue,
                  const std::string& speaker = "")
{
    Subtitle s;
    s.text = text;
    s.durationSeconds = duration;
    s.category = cat;
    s.speaker = speaker;
    return s;
}

// Deterministic measurer used throughout: every character is 10 px wide
// at scale = 1 (font atlas pixel size). Production passes
// TextRenderer::measureTextWidth; tests use this stub so layout math is
// verifiable without a GL context.
auto fixedWidth = [](const std::string& s) -> float
{
    return static_cast<float>(s.size()) * 10.0f;
};

}

// -- styleFor --

TEST(SubtitleRenderer, DialogueStyleUsesYellowSpeakerWhiteBody)
{
    const SubtitleStyle s = styleFor(SubtitleCategory::Dialogue);
    EXPECT_FLOAT_EQ(s.textColor.r,    1.0f);
    EXPECT_FLOAT_EQ(s.textColor.g,    1.0f);
    EXPECT_FLOAT_EQ(s.textColor.b,    1.0f);
    EXPECT_GT(s.speakerColor.r,       0.9f);
    EXPECT_GT(s.speakerColor.g,       0.8f);
    EXPECT_LT(s.speakerColor.b,       0.5f);
}

TEST(SubtitleRenderer, NarratorStyleIsWhite)
{
    const SubtitleStyle s = styleFor(SubtitleCategory::Narrator);
    EXPECT_FLOAT_EQ(s.textColor.r,    1.0f);
    EXPECT_FLOAT_EQ(s.textColor.g,    1.0f);
    EXPECT_FLOAT_EQ(s.textColor.b,    1.0f);
}

TEST(SubtitleRenderer, SoundCueStyleIsCyanGrey)
{
    const SubtitleStyle s = styleFor(SubtitleCategory::SoundCue);
    EXPECT_LT(s.textColor.r,          s.textColor.b);
    EXPECT_GT(s.textColor.b,          0.8f);
}

TEST(SubtitleRenderer, PlateColorIs50PercentBlack)
{
    const SubtitleStyle s = styleFor(SubtitleCategory::Dialogue);
    EXPECT_FLOAT_EQ(s.plateColor.r,   0.0f);
    EXPECT_FLOAT_EQ(s.plateColor.g,   0.0f);
    EXPECT_FLOAT_EQ(s.plateColor.b,   0.0f);
    EXPECT_FLOAT_EQ(s.plateColor.a,   0.5f);
}

// -- computeSubtitleLayout --

TEST(SubtitleRendererLayout, EmptyQueueProducesEmptyLayout)
{
    SubtitleQueue queue;
    SubtitleLayoutParams params;
    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    EXPECT_TRUE(lines.empty());
}

TEST(SubtitleRendererLayout, SingleDialogueLineComposesSpeakerPrefix)
{
    SubtitleQueue queue;
    queue.enqueue(makeLine("Draw near the mountain.", 3.0f,
                           SubtitleCategory::Dialogue, "Moses"));
    SubtitleLayoutParams params;
    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].fullText, "Moses: Draw near the mountain.");
    EXPECT_EQ(lines[0].category, SubtitleCategory::Dialogue);
}

TEST(SubtitleRendererLayout, SoundCueWrapsTextInBrackets)
{
    SubtitleQueue queue;
    queue.enqueue(makeLine("wind howls", 3.0f, SubtitleCategory::SoundCue));
    SubtitleLayoutParams params;
    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].fullText, "[wind howls]");
}

TEST(SubtitleRendererLayout, NarratorHasNoSpeakerPrefix)
{
    SubtitleQueue queue;
    queue.enqueue(makeLine("The heavens declare.", 3.0f,
                           SubtitleCategory::Narrator));
    SubtitleLayoutParams params;
    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].fullText, "The heavens declare.");
}

TEST(SubtitleRendererLayout, BasePixelScalesLinearlyWithViewportHeight)
{
    // At 1080p, Medium preset (1.25×), font pixel size 48:
    //   basePx = 46 × (1080 / 1080) × 1.25 = 57.5
    //   textScale = 57.5 / 48 ≈ 1.1979
    SubtitleQueue queue;
    queue.setSizePreset(SubtitleSizePreset::Medium);
    queue.enqueue(makeLine("test", 1.0f, SubtitleCategory::Narrator));

    SubtitleLayoutParams params1080;
    params1080.screenWidth   = 1920;
    params1080.screenHeight  = 1080;
    params1080.fontPixelSize = 48;
    const auto lines1080 = computeSubtitleLayout(queue, params1080, fixedWidth);
    ASSERT_EQ(lines1080.size(), 1u);
    EXPECT_NEAR(lines1080[0].textScale, 57.5f / 48.0f, 1e-4f);

    // At 2160p (4K), same preset: basePx doubles → textScale doubles.
    SubtitleLayoutParams params2160 = params1080;
    params2160.screenWidth   = 3840;
    params2160.screenHeight  = 2160;
    const auto lines2160 = computeSubtitleLayout(queue, params2160, fixedWidth);
    ASSERT_EQ(lines2160.size(), 1u);
    EXPECT_NEAR(lines2160[0].textScale, 2.0f * lines1080[0].textScale, 1e-4f);
}

TEST(SubtitleRendererLayout, SizePresetMultiplesBasePx)
{
    // Small (1.0×) vs XL (2.0×) at the same resolution should produce
    // exactly a 2× text scale ratio.
    SubtitleLayoutParams params;
    params.screenWidth   = 1920;
    params.screenHeight  = 1080;
    params.fontPixelSize = 48;

    SubtitleQueue small;
    small.setSizePreset(SubtitleSizePreset::Small);
    small.enqueue(makeLine("x", 1.0f, SubtitleCategory::Narrator));
    const auto smallLines = computeSubtitleLayout(small, params, fixedWidth);

    SubtitleQueue xl;
    xl.setSizePreset(SubtitleSizePreset::XL);
    xl.enqueue(makeLine("x", 1.0f, SubtitleCategory::Narrator));
    const auto xlLines = computeSubtitleLayout(xl, params, fixedWidth);

    ASSERT_EQ(smallLines.size(), 1u);
    ASSERT_EQ(xlLines.size(),    1u);
    EXPECT_NEAR(xlLines[0].textScale / smallLines[0].textScale, 2.0f, 1e-4f);
}

TEST(SubtitleRendererLayout, PlateWidthEqualsTextWidthPlusPadding)
{
    SubtitleQueue queue;
    queue.setSizePreset(SubtitleSizePreset::Medium);
    queue.enqueue(makeLine("abcde", 1.0f, SubtitleCategory::Narrator));

    SubtitleLayoutParams params;
    params.screenWidth   = 1920;
    params.screenHeight  = 1080;
    params.fontPixelSize = 48;
    params.platePaddingX = 8.0f;

    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    ASSERT_EQ(lines.size(), 1u);
    // 5 chars × 10 px × textScale + 16 px padding
    const float textScale = lines[0].textScale;
    EXPECT_NEAR(lines[0].plateSize.x,
                5.0f * 10.0f * textScale + 16.0f, 1e-3f);
}

TEST(SubtitleRendererLayout, PlateIsHorizontallyCentered)
{
    SubtitleQueue queue;
    queue.enqueue(makeLine("centered", 1.0f, SubtitleCategory::Narrator));

    SubtitleLayoutParams params;
    params.screenWidth   = 1920;
    params.screenHeight  = 1080;
    params.fontPixelSize = 48;

    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    ASSERT_EQ(lines.size(), 1u);
    const float leftMargin  = lines[0].platePos.x;
    const float rightMargin = static_cast<float>(params.screenWidth)
                              - (lines[0].platePos.x + lines[0].plateSize.x);
    EXPECT_NEAR(leftMargin, rightMargin, 1e-3f);
}

TEST(SubtitleRendererLayout, NewestCaptionIsAtBottom)
{
    SubtitleQueue queue;
    queue.enqueue(makeLine("oldest",  3.0f, SubtitleCategory::Narrator));
    queue.enqueue(makeLine("middle",  3.0f, SubtitleCategory::Narrator));
    queue.enqueue(makeLine("newest",  3.0f, SubtitleCategory::Narrator));

    SubtitleLayoutParams params;
    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    ASSERT_EQ(lines.size(), 3u);

    // activeSubtitles() order matches enqueue order. Layout stacks
    // newest-at-bottom, so the *last* line in the output should have
    // the largest Y (furthest from top / closest to bottom).
    EXPECT_LT(lines[0].platePos.y, lines[1].platePos.y);
    EXPECT_LT(lines[1].platePos.y, lines[2].platePos.y);

    // All three plates share the same size (same text length, same
    // preset), so the vertical gap between each should be constant.
    const float gap01 = lines[1].platePos.y - lines[0].platePos.y;
    const float gap12 = lines[2].platePos.y - lines[1].platePos.y;
    EXPECT_NEAR(gap01, gap12, 1e-3f);
}

TEST(SubtitleRendererLayout, BottomMarginRespectsFraction)
{
    SubtitleQueue queue;
    queue.enqueue(makeLine("x", 1.0f, SubtitleCategory::Narrator));

    SubtitleLayoutParams params;
    params.screenWidth     = 1920;
    params.screenHeight    = 1080;
    params.bottomMarginFrac= 0.12f;
    params.fontPixelSize   = 48;

    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    ASSERT_EQ(lines.size(), 1u);
    // Plate bottom edge (= platePos.y + plateSize.y) should sit at
    // screenHeight × (1 - bottomMarginFrac) = 1080 × 0.88 = 950.4.
    const float plateBottom = lines[0].platePos.y + lines[0].plateSize.y;
    EXPECT_NEAR(plateBottom,
                static_cast<float>(params.screenHeight) *
                (1.0f - params.bottomMarginFrac),
                1e-3f);
}

TEST(SubtitleRendererLayout, TextBaselineIsInsidePlate)
{
    SubtitleQueue queue;
    queue.enqueue(makeLine("x", 1.0f, SubtitleCategory::Narrator));

    SubtitleLayoutParams params;
    params.platePaddingX = 8.0f;
    params.platePaddingY = 4.0f;

    const auto lines = computeSubtitleLayout(queue, params, fixedWidth);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NEAR(lines[0].textBaseline.x,
                lines[0].platePos.x + params.platePaddingX, 1e-3f);
    EXPECT_NEAR(lines[0].textBaseline.y,
                lines[0].platePos.y + params.platePaddingY, 1e-3f);
}

TEST(SubtitleRendererLayout, NullMeasureCallableProducesPaddingOnlyPlate)
{
    SubtitleQueue queue;
    queue.enqueue(makeLine("anything", 1.0f, SubtitleCategory::Narrator));

    SubtitleLayoutParams params;
    params.platePaddingX = 8.0f;

    // Passing an empty std::function is treated as "width = 0".
    const auto lines = computeSubtitleLayout(
        queue, params, std::function<float(const std::string&)>{});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NEAR(lines[0].plateSize.x, 2.0f * params.platePaddingX, 1e-3f);
}
