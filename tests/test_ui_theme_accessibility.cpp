// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_theme_accessibility.cpp
/// @brief Phase 10 accessibility coverage for the UI theme: scale presets
///        and high-contrast mode.

#include <gtest/gtest.h>

#include "systems/ui_system.h"
#include "ui/ui_theme.h"

using namespace Vestige;

// -- Scale presets --

TEST(UITheme_Scale, PresetFactorsAreExpectedValues)
{
    EXPECT_FLOAT_EQ(scaleFactorOf(UIScalePreset::X1_0), 1.00f);
    EXPECT_FLOAT_EQ(scaleFactorOf(UIScalePreset::X1_25), 1.25f);
    EXPECT_FLOAT_EQ(scaleFactorOf(UIScalePreset::X1_5), 1.50f);
    EXPECT_FLOAT_EQ(scaleFactorOf(UIScalePreset::X2_0), 2.00f);
}

TEST(UITheme_Scale, WithScaleMultipliesEveryPixelSize)
{
    UITheme base = UITheme::defaultTheme();
    UITheme scaled = base.withScale(1.5f);

    EXPECT_FLOAT_EQ(scaled.defaultTextScale,       base.defaultTextScale       * 1.5f);
    EXPECT_FLOAT_EQ(scaled.crosshairLength,        base.crosshairLength        * 1.5f);
    EXPECT_FLOAT_EQ(scaled.crosshairThickness,     base.crosshairThickness     * 1.5f);
    EXPECT_FLOAT_EQ(scaled.progressBarHeight,      base.progressBarHeight      * 1.5f);
    EXPECT_FLOAT_EQ(scaled.panelBorderWidth,       base.panelBorderWidth       * 1.5f);
    EXPECT_FLOAT_EQ(scaled.buttonHeight,           base.buttonHeight           * 1.5f);
    EXPECT_FLOAT_EQ(scaled.buttonHeightSmall,      base.buttonHeightSmall      * 1.5f);
    EXPECT_FLOAT_EQ(scaled.buttonPadX,             base.buttonPadX             * 1.5f);
    EXPECT_FLOAT_EQ(scaled.buttonAccentTickWidth,  base.buttonAccentTickWidth  * 1.5f);
    EXPECT_FLOAT_EQ(scaled.sliderHeight,           base.sliderHeight           * 1.5f);
    EXPECT_FLOAT_EQ(scaled.sliderTrackHeight,      base.sliderTrackHeight      * 1.5f);
    EXPECT_FLOAT_EQ(scaled.sliderThumbSize,        base.sliderThumbSize        * 1.5f);
    EXPECT_FLOAT_EQ(scaled.sliderThumbBorder,      base.sliderThumbBorder      * 1.5f);
    EXPECT_FLOAT_EQ(scaled.checkboxSize,           base.checkboxSize           * 1.5f);
    EXPECT_FLOAT_EQ(scaled.checkboxStroke,         base.checkboxStroke         * 1.5f);
    EXPECT_FLOAT_EQ(scaled.dropdownHeight,         base.dropdownHeight         * 1.5f);
    EXPECT_FLOAT_EQ(scaled.dropdownMinWidth,       base.dropdownMinWidth       * 1.5f);
    EXPECT_FLOAT_EQ(scaled.dropdownMenuMaxHeight,  base.dropdownMenuMaxHeight  * 1.5f);
    EXPECT_FLOAT_EQ(scaled.keybindKeyHeight,       base.keybindKeyHeight       * 1.5f);
    EXPECT_FLOAT_EQ(scaled.keybindKeyMinWidth,     base.keybindKeyMinWidth     * 1.5f);
    EXPECT_FLOAT_EQ(scaled.settingRowControlWidth, base.settingRowControlWidth * 1.5f);
    EXPECT_FLOAT_EQ(scaled.settingRowVerticalPad,  base.settingRowVerticalPad  * 1.5f);
    EXPECT_FLOAT_EQ(scaled.typeDisplay,            base.typeDisplay            * 1.5f);
    EXPECT_FLOAT_EQ(scaled.typeH1,                 base.typeH1                 * 1.5f);
    EXPECT_FLOAT_EQ(scaled.typeH2,                 base.typeH2                 * 1.5f);
    EXPECT_FLOAT_EQ(scaled.typeBody,               base.typeBody               * 1.5f);
    EXPECT_FLOAT_EQ(scaled.typeButton,             base.typeButton             * 1.5f);
    EXPECT_FLOAT_EQ(scaled.typeCaption,            base.typeCaption            * 1.5f);
    EXPECT_FLOAT_EQ(scaled.typeMicro,              base.typeMicro              * 1.5f);
    EXPECT_FLOAT_EQ(scaled.focusRingThickness,     base.focusRingThickness     * 1.5f);
    EXPECT_FLOAT_EQ(scaled.focusRingOffset,        base.focusRingOffset        * 1.5f);
}

TEST(UITheme_Scale, WithScaleLeavesColoursAndMotionUntouched)
{
    UITheme base = UITheme::defaultTheme();
    UITheme scaled = base.withScale(2.0f);

    EXPECT_EQ(scaled.bgBase,        base.bgBase);
    EXPECT_EQ(scaled.textPrimary,   base.textPrimary);
    EXPECT_EQ(scaled.accent,        base.accent);
    EXPECT_EQ(scaled.crosshair,     base.crosshair);
    EXPECT_FLOAT_EQ(scaled.transitionDuration, base.transitionDuration);
    EXPECT_EQ(scaled.fontUI,        base.fontUI);
    EXPECT_EQ(scaled.fontDisplay,   base.fontDisplay);
    EXPECT_EQ(scaled.fontMono,      base.fontMono);
}

TEST(UITheme_Scale, Identity1_0IsAFixedPoint)
{
    UITheme base = UITheme::defaultTheme();
    UITheme scaled = base.withScale(1.0f);
    EXPECT_FLOAT_EQ(scaled.buttonHeight,   base.buttonHeight);
    EXPECT_FLOAT_EQ(scaled.typeBody,       base.typeBody);
    EXPECT_FLOAT_EQ(scaled.checkboxStroke, base.checkboxStroke);
}

// -- High-contrast mode --

TEST(UITheme_HighContrast, BackgroundIsPureBlackAndTextIsPureWhite)
{
    UITheme t = UITheme::defaultTheme().withHighContrast();

    EXPECT_FLOAT_EQ(t.bgBase.r, 0.0f);
    EXPECT_FLOAT_EQ(t.bgBase.g, 0.0f);
    EXPECT_FLOAT_EQ(t.bgBase.b, 0.0f);
    EXPECT_FLOAT_EQ(t.bgBase.a, 1.0f);

    EXPECT_FLOAT_EQ(t.textPrimary.r, 1.0f);
    EXPECT_FLOAT_EQ(t.textPrimary.g, 1.0f);
    EXPECT_FLOAT_EQ(t.textPrimary.b, 1.0f);
}

TEST(UITheme_HighContrast, PanelStrokesAreFullAlpha)
{
    UITheme t = UITheme::defaultTheme().withHighContrast();
    EXPECT_FLOAT_EQ(t.panelStroke.a,       1.0f);
    EXPECT_FLOAT_EQ(t.panelStrokeStrong.a, 1.0f);
}

TEST(UITheme_HighContrast, LeavesSizesUnchanged)
{
    UITheme base = UITheme::defaultTheme();
    UITheme hc = base.withHighContrast();
    EXPECT_FLOAT_EQ(hc.buttonHeight,      base.buttonHeight);
    EXPECT_FLOAT_EQ(hc.typeBody,          base.typeBody);
    EXPECT_FLOAT_EQ(hc.crosshairLength,   base.crosshairLength);
    EXPECT_FLOAT_EQ(hc.focusRingThickness, base.focusRingThickness);
}

TEST(UITheme_HighContrast, DisabledTextRemainsDiscriminableAgainstBlackBg)
{
    UITheme t = UITheme::defaultTheme().withHighContrast();
    // Disabled text grey must not collapse into the black background —
    // keep above a conservative 0.4 brightness floor.
    EXPECT_GT(t.textDisabled.r, 0.40f);
    EXPECT_GT(t.textDisabled.g, 0.40f);
    EXPECT_GT(t.textDisabled.b, 0.40f);
}

// -- UISystem integration --

TEST(UISystemTheme, DefaultIsX1_0AndNotHighContrast)
{
    UISystem ui;
    EXPECT_EQ(ui.getScalePreset(), UIScalePreset::X1_0);
    EXPECT_FALSE(ui.isHighContrastMode());
    // Active theme matches base when nothing is applied.
    EXPECT_FLOAT_EQ(ui.getTheme().buttonHeight,
                    ui.getBaseTheme().buttonHeight);
}

TEST(UISystemTheme, SetScalePresetRebuildsActiveTheme)
{
    UISystem ui;
    const float basePx = ui.getBaseTheme().buttonHeight;
    ui.setScalePreset(UIScalePreset::X1_5);
    EXPECT_EQ(ui.getScalePreset(), UIScalePreset::X1_5);
    EXPECT_FLOAT_EQ(ui.getTheme().buttonHeight, basePx * 1.5f);
    // Base theme must stay unscaled — canonical source for later rebuilds.
    EXPECT_FLOAT_EQ(ui.getBaseTheme().buttonHeight, basePx);
}

TEST(UISystemTheme, SetHighContrastRebuildsActiveTheme)
{
    UISystem ui;
    ui.setHighContrastMode(true);
    EXPECT_TRUE(ui.isHighContrastMode());
    EXPECT_FLOAT_EQ(ui.getTheme().bgBase.r, 0.0f);
    EXPECT_FLOAT_EQ(ui.getTheme().textPrimary.r, 1.0f);
    // Base theme keeps its Vellum palette.
    EXPECT_GT(ui.getBaseTheme().textPrimary.r, 0.0f);
    EXPECT_LT(ui.getBaseTheme().textPrimary.r, 1.0f);
}

TEST(UISystemTheme, HighContrastAndScaleCompose)
{
    UISystem ui;
    const float basePx = ui.getBaseTheme().buttonHeight;
    ui.setScalePreset(UIScalePreset::X2_0);
    ui.setHighContrastMode(true);

    // Both applied simultaneously.
    EXPECT_FLOAT_EQ(ui.getTheme().buttonHeight, basePx * 2.0f);
    EXPECT_FLOAT_EQ(ui.getTheme().bgBase.r, 0.0f);
    EXPECT_FLOAT_EQ(ui.getTheme().textPrimary.r, 1.0f);
}

TEST(UISystemTheme, TogglingHighContrastOffReturnsBasePalette)
{
    UISystem ui;
    const float baseR = ui.getBaseTheme().textPrimary.r;

    ui.setHighContrastMode(true);
    ui.setHighContrastMode(false);

    EXPECT_FALSE(ui.isHighContrastMode());
    EXPECT_FLOAT_EQ(ui.getTheme().textPrimary.r, baseR);
}

TEST(UISystemTheme, SetBaseThemeKeepsScaleApplied)
{
    UISystem ui;
    ui.setScalePreset(UIScalePreset::X1_5);

    UITheme plumb = UITheme::plumbline();
    ui.setBaseTheme(plumb);

    EXPECT_FLOAT_EQ(ui.getTheme().buttonHeight, plumb.buttonHeight * 1.5f);
    // Palette now reflects Plumbline's cold near-black.
    EXPECT_FLOAT_EQ(ui.getTheme().bgBase.r, plumb.bgBase.r);
}

// -- Reduced motion --

TEST(UITheme_ReducedMotion, ZerosTransitionDuration)
{
    UITheme base = UITheme::defaultTheme();
    UITheme rm   = base.withReducedMotion();
    EXPECT_GT(base.transitionDuration, 0.0f);
    EXPECT_FLOAT_EQ(rm.transitionDuration, 0.0f);
}

TEST(UITheme_ReducedMotion, LeavesPaletteAndSizesUntouched)
{
    UITheme base = UITheme::defaultTheme();
    UITheme rm   = base.withReducedMotion();
    EXPECT_EQ(rm.bgBase,        base.bgBase);
    EXPECT_EQ(rm.textPrimary,   base.textPrimary);
    EXPECT_EQ(rm.accent,        base.accent);
    EXPECT_FLOAT_EQ(rm.buttonHeight,       base.buttonHeight);
    EXPECT_FLOAT_EQ(rm.typeBody,           base.typeBody);
    EXPECT_FLOAT_EQ(rm.focusRingThickness, base.focusRingThickness);
}

TEST(UISystemTheme, SetReducedMotionRebuildsActiveTheme)
{
    UISystem ui;
    EXPECT_FALSE(ui.isReducedMotion());
    EXPECT_GT(ui.getTheme().transitionDuration, 0.0f);

    ui.setReducedMotion(true);
    EXPECT_TRUE(ui.isReducedMotion());
    EXPECT_FLOAT_EQ(ui.getTheme().transitionDuration, 0.0f);

    // Base theme keeps its original motion timing — canonical source.
    EXPECT_GT(ui.getBaseTheme().transitionDuration, 0.0f);
}

TEST(UISystemTheme, ReducedMotionComposesWithScaleAndHighContrast)
{
    UISystem ui;
    const float baseBtn = ui.getBaseTheme().buttonHeight;

    ui.setScalePreset(UIScalePreset::X2_0);
    ui.setHighContrastMode(true);
    ui.setReducedMotion(true);

    // All three applied simultaneously.
    EXPECT_FLOAT_EQ(ui.getTheme().buttonHeight,       baseBtn * 2.0f);
    EXPECT_FLOAT_EQ(ui.getTheme().bgBase.r,           0.0f);
    EXPECT_FLOAT_EQ(ui.getTheme().textPrimary.r,      1.0f);
    EXPECT_FLOAT_EQ(ui.getTheme().transitionDuration, 0.0f);
}

TEST(UISystemTheme, TogglingReducedMotionOffRestoresTransitionDuration)
{
    UISystem ui;
    const float baseTransition = ui.getBaseTheme().transitionDuration;

    ui.setReducedMotion(true);
    ui.setReducedMotion(false);

    EXPECT_FALSE(ui.isReducedMotion());
    EXPECT_FLOAT_EQ(ui.getTheme().transitionDuration, baseTransition);
}

// =============================================================================
// Phase 10.9 Slice 3 S9 — WCAG default contrast.
//
// Vellum `panelStroke` at alpha 0.22 composites to ~1.6:1 against the deep
// walnut-ink background — below WCAG 1.4.11's 3:1 bar for non-text UI
// components. `textDisabled` at #5C5447 gives ~2.4:1 against the same
// background — below the 4.5:1 threshold the ROADMAP targets for
// comfort on the partially-sighted primary user. Plumbline has the same
// shape with different numerics. High-contrast mode must clear WCAG 2.2
// AAA (>= 7:1) since it exists specifically to serve the strongest
// accessibility tier.
//
// The `ui_contrast` free functions exist so the WCAG bit-math is
// independently testable — palette changes can be verified arithmetically
// in CI instead of only by eye.
// =============================================================================

// -- ui_contrast helpers --

TEST(UIContrast, RelativeLuminanceBlackIsZero_S9)
{
    EXPECT_NEAR(ui_contrast::relativeLuminance(glm::vec3(0.0f)), 0.0f, 1e-6f);
}

TEST(UIContrast, RelativeLuminanceWhiteIsOne_S9)
{
    EXPECT_NEAR(ui_contrast::relativeLuminance(glm::vec3(1.0f)), 1.0f, 1e-6f);
}

TEST(UIContrast, ContrastBlackOnWhiteIs21_S9)
{
    // Canonical WCAG reference: (1 + 0.05) / (0 + 0.05) = 21.0.
    EXPECT_NEAR(
        ui_contrast::contrastRatio(glm::vec3(0.0f), glm::vec3(1.0f)),
        21.0f, 0.01f);
}

TEST(UIContrast, ContrastIdenticalColoursIsOne_S9)
{
    EXPECT_NEAR(
        ui_contrast::contrastRatio(glm::vec3(0.5f), glm::vec3(0.5f)),
        1.0f, 1e-5f);
}

TEST(UIContrast, ContrastIsOrderIndependent_S9)
{
    const glm::vec3 a(0.1f, 0.2f, 0.3f);
    const glm::vec3 b(0.8f, 0.7f, 0.9f);
    EXPECT_FLOAT_EQ(ui_contrast::contrastRatio(a, b),
                    ui_contrast::contrastRatio(b, a))
        << "WCAG contrast is symmetric — the formula divides brighter by "
           "darker regardless of which argument came first.";
}

TEST(UIContrast, CompositeOverAlphaZeroReturnsBackground_S9)
{
    const glm::vec4 fg(0.8f, 0.2f, 0.1f, 0.0f);
    const glm::vec3 bg(0.1f, 0.2f, 0.3f);
    const glm::vec3 out = ui_contrast::compositeOver(fg, bg);
    EXPECT_FLOAT_EQ(out.r, bg.r);
    EXPECT_FLOAT_EQ(out.g, bg.g);
    EXPECT_FLOAT_EQ(out.b, bg.b);
}

TEST(UIContrast, CompositeOverAlphaOneReturnsForeground_S9)
{
    const glm::vec4 fg(0.8f, 0.2f, 0.1f, 1.0f);
    const glm::vec3 bg(0.1f, 0.2f, 0.3f);
    const glm::vec3 out = ui_contrast::compositeOver(fg, bg);
    EXPECT_FLOAT_EQ(out.r, fg.r);
    EXPECT_FLOAT_EQ(out.g, fg.g);
    EXPECT_FLOAT_EQ(out.b, fg.b);
}

TEST(UIContrast, CompositeOverHalfAlphaIsMidpoint_S9)
{
    // Straight-alpha blend: out = a * fg + (1 - a) * bg.
    const glm::vec4 fg(1.0f, 1.0f, 1.0f, 0.5f);
    const glm::vec3 bg(0.0f, 0.0f, 0.0f);
    const glm::vec3 out = ui_contrast::compositeOver(fg, bg);
    EXPECT_NEAR(out.r, 0.5f, 1e-6f);
    EXPECT_NEAR(out.g, 0.5f, 1e-6f);
    EXPECT_NEAR(out.b, 0.5f, 1e-6f);
}

// -- Vellum (default) register --

TEST(UIThemeContrast, VellumTextDisabledMeetsWcag_1_4_3_Comfort_S9)
{
    // WCAG 1.4.3 Contrast (Minimum) targets 4.5:1 for body text.
    // ROADMAP S9 applies the same bar to `textDisabled` so dimmed
    // labels remain legible — disabled is not "invisible" to a
    // partially-sighted user.
    const UITheme t = UITheme::defaultTheme();
    const float ratio =
        ui_contrast::contrastRatio(t.textDisabled, glm::vec3(t.bgBase));
    EXPECT_GE(ratio, 4.5f)
        << "Vellum textDisabled / bgBase contrast = " << ratio
        << " — must be >= 4.5:1 per ROADMAP S9 (WCAG 1.4.3 comfort bar).";
}

TEST(UIThemeContrast, VellumPanelStrokeMeetsWcag_1_4_11_S9)
{
    // WCAG 1.4.11 Non-text Contrast — 3:1 for UI-component
    // boundaries. `panelStroke` is alpha-blended, so composite the
    // stroke over the panel background before computing.
    const UITheme t = UITheme::defaultTheme();
    const glm::vec3 composite =
        ui_contrast::compositeOver(t.panelStroke, glm::vec3(t.bgBase));
    const float ratio =
        ui_contrast::contrastRatio(composite, glm::vec3(t.bgBase));
    EXPECT_GE(ratio, 3.0f)
        << "Vellum panelStroke (composited) / bgBase contrast = " << ratio
        << " — must be >= 3:1 per WCAG 1.4.11.";
}

TEST(UIThemeContrast, VellumStrokeStrongStillBrighterThanStroke_S9)
{
    // The hover/active stroke must visually read as emphasised
    // relative to the at-rest stroke — preserve the design
    // invariant under S9's alpha bump.
    const UITheme t = UITheme::defaultTheme();
    EXPECT_GT(t.panelStrokeStrong.a, t.panelStroke.a)
        << "panelStrokeStrong must remain a louder version of panelStroke "
           "so hover/active state is discernable post-S9.";
}

// -- Plumbline register --

TEST(UIThemeContrast, PlumblineTextDisabledMeetsWcag_1_4_3_Comfort_S9)
{
    const UITheme t = UITheme::plumbline();
    const float ratio =
        ui_contrast::contrastRatio(t.textDisabled, glm::vec3(t.bgBase));
    EXPECT_GE(ratio, 4.5f)
        << "Plumbline textDisabled / bgBase contrast = " << ratio
        << " — must be >= 4.5:1 per ROADMAP S9.";
}

TEST(UIThemeContrast, PlumblinePanelStrokeMeetsWcag_1_4_11_S9)
{
    const UITheme t = UITheme::plumbline();
    const glm::vec3 composite =
        ui_contrast::compositeOver(t.panelStroke, glm::vec3(t.bgBase));
    const float ratio =
        ui_contrast::contrastRatio(composite, glm::vec3(t.bgBase));
    EXPECT_GE(ratio, 3.0f)
        << "Plumbline panelStroke (composited) / bgBase contrast = " << ratio
        << " — must be >= 3:1 per WCAG 1.4.11.";
}

TEST(UIThemeContrast, PlumblineStrokeStrongStillBrighterThanStroke_S9)
{
    const UITheme t = UITheme::plumbline();
    EXPECT_GT(t.panelStrokeStrong.a, t.panelStroke.a);
}

// -- High-contrast mode (WCAG 2.2 AAA tier) --

TEST(UIThemeContrast, HighContrastTextPrimaryMeetsWcag_1_4_6_AAA_S9)
{
    // 1.4.6 Enhanced Contrast — 7:1 for body text. High-contrast
    // mode exists to serve the AAA tier explicitly.
    const UITheme t = UITheme::defaultTheme().withHighContrast();
    const float ratio =
        ui_contrast::contrastRatio(t.textPrimary, glm::vec3(t.bgBase));
    EXPECT_GE(ratio, 7.0f)
        << "High-contrast textPrimary / bgBase contrast = " << ratio
        << " — high-contrast mode must satisfy WCAG 2.2 AAA (>= 7:1).";
}

TEST(UIThemeContrast, HighContrastTextDisabledStillMeets_1_4_3_S9)
{
    // Disabled text in HC mode must still clear the 4.5:1 bar —
    // otherwise HC regresses below Vellum for disabled labels.
    const UITheme t = UITheme::defaultTheme().withHighContrast();
    const float ratio =
        ui_contrast::contrastRatio(t.textDisabled, glm::vec3(t.bgBase));
    EXPECT_GE(ratio, 4.5f);
}
