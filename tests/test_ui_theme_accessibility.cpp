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
