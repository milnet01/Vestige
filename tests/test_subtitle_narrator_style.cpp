// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_subtitle_narrator_style.cpp
/// @brief Phase 10.9 Slice 2 P6 — narrator styling (both paths).
///
/// P6 was blocked on an asset-source decision between the original
/// `docs/phases/phase_10_7_design.md §4.2` spec ("Narrator — italic white") — which
/// requires an italic font file the project does not ship — and a
/// spec-revised accessibility-best alternative (warm-amber colour
/// differentiation). The chosen resolution ships BOTH paths and
/// exposes a runtime selector on `SubtitleQueue` so the game developer
/// picks at the integration seam:
///
///   * `SubtitleNarratorStyle::Italic` — white text rendered with an
///     oblique horizontal shear (no second font atlas needed; the
///     existing atlas is sheared at vertex-emit time to approximate
///     italic; ~11° standard typographic slant).
///   * `SubtitleNarratorStyle::Colour` — upright text in warm-amber
///     (the theme accent family). Default — matches the partially-
///     sighted-primary-user priority (italic is harder to read for
///     low-vision users; colour differentiation plus the absent
///     speaker prefix distinguishes narrator without typography).
///
/// Dialogue and SoundCue categories are unaffected by the selector —
/// they're pinned by regression tests here so a future narrator tweak
/// can't silently recolour them.

#include "ui/subtitle.h"
#include "ui/subtitle_renderer.h"
#include "renderer/text_renderer.h"

#include <gtest/gtest.h>

using namespace Vestige;

// ---------------------------------------------------------------------------
// SubtitleQueue: narrator-style preference getter / setter.
// ---------------------------------------------------------------------------

TEST(SubtitleNarratorStyle, DefaultIsColour_P6)
{
    // Accessibility-first default: colour differentiation is more
    // readable for low-vision users than italic typography.
    SubtitleQueue q;
    EXPECT_EQ(q.narratorStyle(), SubtitleNarratorStyle::Colour);
}

TEST(SubtitleNarratorStyle, SetterRoundTrip_P6)
{
    SubtitleQueue q;
    q.setNarratorStyle(SubtitleNarratorStyle::Italic);
    EXPECT_EQ(q.narratorStyle(), SubtitleNarratorStyle::Italic);

    q.setNarratorStyle(SubtitleNarratorStyle::Colour);
    EXPECT_EQ(q.narratorStyle(), SubtitleNarratorStyle::Colour);
}

// ---------------------------------------------------------------------------
// styleFor(Narrator, ...) — the two paths.
// ---------------------------------------------------------------------------

TEST(SubtitleNarratorStyle, ColourPathUsesWarmAmberNotItalic_P6)
{
    const SubtitleStyle s =
        styleFor(SubtitleCategory::Narrator, SubtitleNarratorStyle::Colour);

    // Warm-amber: red component dominant, blue suppressed. Distinct
    // from dialogue white (all channels 1.0) and sound-cue cyan-grey
    // (blue + green > red).
    EXPECT_GT(s.textColor.r, s.textColor.b);
    EXPECT_GT(s.textColor.r, 0.6f);
    EXPECT_LT(s.textColor.b, 0.4f);
    EXPECT_FALSE(s.italic);
}

TEST(SubtitleNarratorStyle, ItalicPathIsWhiteAndItalic_P6)
{
    const SubtitleStyle s =
        styleFor(SubtitleCategory::Narrator, SubtitleNarratorStyle::Italic);

    EXPECT_FLOAT_EQ(s.textColor.r, 1.0f);
    EXPECT_FLOAT_EQ(s.textColor.g, 1.0f);
    EXPECT_FLOAT_EQ(s.textColor.b, 1.0f);
    EXPECT_TRUE(s.italic);
}

TEST(SubtitleNarratorStyle, NarratorStylesAreVisuallyDistinct_P6)
{
    // Cross-path pin: the two narrator modes must be recognisably
    // different — either colour or italic flag must differ. (If
    // both match, there's nothing for the developer to toggle.)
    const SubtitleStyle italic =
        styleFor(SubtitleCategory::Narrator, SubtitleNarratorStyle::Italic);
    const SubtitleStyle colour =
        styleFor(SubtitleCategory::Narrator, SubtitleNarratorStyle::Colour);

    const bool colourDiffers = (italic.textColor != colour.textColor);
    const bool italicDiffers = (italic.italic != colour.italic);
    EXPECT_TRUE(colourDiffers || italicDiffers);
}

// ---------------------------------------------------------------------------
// Regression pins: Dialogue + SoundCue untouched by the narrator toggle.
// ---------------------------------------------------------------------------

TEST(SubtitleNarratorStyle, DialogueUnchangedByNarratorToggle_P6)
{
    const SubtitleStyle i =
        styleFor(SubtitleCategory::Dialogue, SubtitleNarratorStyle::Italic);
    const SubtitleStyle c =
        styleFor(SubtitleCategory::Dialogue, SubtitleNarratorStyle::Colour);

    EXPECT_EQ(i.textColor, c.textColor);
    EXPECT_EQ(i.speakerColor, c.speakerColor);
    EXPECT_FALSE(i.italic);
    EXPECT_FALSE(c.italic);

    // Yellow speaker label (TLOU2-style) survives the new path.
    EXPECT_GT(c.speakerColor.r, 0.9f);
    EXPECT_GT(c.speakerColor.g, 0.8f);
    EXPECT_LT(c.speakerColor.b, 0.5f);
}

TEST(SubtitleNarratorStyle, SoundCueUnchangedByNarratorToggle_P6)
{
    const SubtitleStyle i =
        styleFor(SubtitleCategory::SoundCue, SubtitleNarratorStyle::Italic);
    const SubtitleStyle c =
        styleFor(SubtitleCategory::SoundCue, SubtitleNarratorStyle::Colour);

    EXPECT_EQ(i.textColor, c.textColor);
    EXPECT_FALSE(i.italic);
    EXPECT_FALSE(c.italic);
}

// ---------------------------------------------------------------------------
// computeSubtitleLayout propagates the italic flag into the layout output.
// ---------------------------------------------------------------------------

TEST(SubtitleNarratorStyle, LayoutPropagatesItalicFlagWhenItalicSelected_P6)
{
    SubtitleQueue q;
    q.setNarratorStyle(SubtitleNarratorStyle::Italic);

    Subtitle s;
    s.text             = "Meanwhile, in Sodom...";
    s.category         = SubtitleCategory::Narrator;
    s.durationSeconds  = 3.0f;
    q.enqueue(s);

    SubtitleLayoutParams params;
    const auto layout = computeSubtitleLayout(
        q, params,
        [](const std::string& row) { return static_cast<float>(row.size()) * 10.0f; });

    ASSERT_EQ(layout.size(), 1u);
    EXPECT_TRUE(layout[0].italic);
}

TEST(SubtitleNarratorStyle, LayoutColourStyleIsNotItalic_P6)
{
    SubtitleQueue q;
    q.setNarratorStyle(SubtitleNarratorStyle::Colour);

    Subtitle s;
    s.text             = "Meanwhile, in Sodom...";
    s.category         = SubtitleCategory::Narrator;
    s.durationSeconds  = 3.0f;
    q.enqueue(s);

    SubtitleLayoutParams params;
    const auto layout = computeSubtitleLayout(
        q, params,
        [](const std::string& row) { return static_cast<float>(row.size()) * 10.0f; });

    ASSERT_EQ(layout.size(), 1u);
    EXPECT_FALSE(layout[0].italic);
}

TEST(SubtitleNarratorStyle, DialogueLayoutNeverItalicRegardlessOfSelector_P6)
{
    SubtitleQueue q;
    q.setNarratorStyle(SubtitleNarratorStyle::Italic);  // narrator selector

    Subtitle s;
    s.text             = "Who goes there?";
    s.speaker          = "Joshua";
    s.category         = SubtitleCategory::Dialogue;
    s.durationSeconds  = 3.0f;
    q.enqueue(s);

    SubtitleLayoutParams params;
    const auto layout = computeSubtitleLayout(
        q, params,
        [](const std::string& row) { return static_cast<float>(row.size()) * 10.0f; });

    ASSERT_EQ(layout.size(), 1u);
    // Dialogue must NEVER inherit the narrator selector's italic flag —
    // only the narrator category branches on the selector.
    EXPECT_FALSE(layout[0].italic);
}

// ---------------------------------------------------------------------------
// Oblique-shear math (pure helper, no GL dependency).
// ---------------------------------------------------------------------------

TEST(TextOblique, AtBaselineIdentity_P6)
{
    // A vertex at exactly the baseline does not move.
    EXPECT_FLOAT_EQ(text_oblique::applyShear(10.0f, 100.0f, 100.0f, 0.2f), 10.0f);
}

TEST(TextOblique, AboveBaselineShiftsRight_P6)
{
    // Top-left-origin: smaller Y is higher on screen. A vertex
    // 20 px above baseline shears right by 20 * factor.
    EXPECT_FLOAT_EQ(text_oblique::applyShear(10.0f, 80.0f, 100.0f, 0.2f), 14.0f);
}

TEST(TextOblique, BelowBaselineShiftsLeft_P6)
{
    // Descenders (g, p, q) extend below baseline — they lean back
    // in italic typography, so the shear there is negative.
    EXPECT_FLOAT_EQ(text_oblique::applyShear(10.0f, 120.0f, 100.0f, 0.2f), 6.0f);
}

TEST(TextOblique, ZeroFactorIsIdentity_P6)
{
    EXPECT_FLOAT_EQ(text_oblique::applyShear(10.0f, 50.0f, 100.0f, 0.0f), 10.0f);
    EXPECT_FLOAT_EQ(text_oblique::applyShear(10.0f, 200.0f, 100.0f, 0.0f), 10.0f);
}
