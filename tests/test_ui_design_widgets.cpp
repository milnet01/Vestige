// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_design_widgets.cpp
/// @brief CPU-side tests for the Phase 9C widget set translated from the
///        Claude Design `vestige-ui-hud-inworld` hand-off.

#include <gtest/gtest.h>

#include "ui/ui_button.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_keybind_row.h"
#include "ui/ui_slider.h"
#include "ui/ui_theme.h"

using namespace Vestige;

// -- UITheme: Vellum / Plumbline registers --

TEST(UIThemeRegisters, VellumDefaultUsesWarmBoneText)
{
    UITheme t = UITheme::defaultTheme();
    // Warm bone text — R > G > B with the design's #EADFC7 ratio.
    EXPECT_GT(t.textPrimary.r, t.textPrimary.g);
    EXPECT_GT(t.textPrimary.g, t.textPrimary.b);
    EXPECT_NEAR(t.textPrimary.r, 0.918f, 0.01f);
}

TEST(UIThemeRegisters, PlumblineUsesCoolerNearBlackBackground)
{
    UITheme p = UITheme::plumbline();
    UITheme v = UITheme::defaultTheme();
    // Plumbline is darker than Vellum across the board.
    EXPECT_LT(p.bgBase.r, v.bgBase.r);
    EXPECT_LT(p.bgBase.g, v.bgBase.g);
    EXPECT_LT(p.bgBase.b, v.bgBase.b);
}

TEST(UIThemeRegisters, AccentInkContrastsAccent)
{
    // accentInk is the foreground colour drawn ON the accent fill — it must
    // contrast strongly so primary buttons are readable.
    UITheme t = UITheme::defaultTheme();
    const float accentLuma = 0.299f * t.accent.r + 0.587f * t.accent.g + 0.114f * t.accent.b;
    const float inkLuma    = 0.299f * t.accentInk.r + 0.587f * t.accentInk.g + 0.114f * t.accentInk.b;
    EXPECT_GT(accentLuma - inkLuma, 0.4f)
        << "accent / accentInk contrast too low for readable primary buttons";
}

TEST(UIThemeRegisters, BothRegistersShareComponentSizing)
{
    UITheme v = UITheme::defaultTheme();
    UITheme p = UITheme::plumbline();
    EXPECT_FLOAT_EQ(v.buttonHeight,        p.buttonHeight);
    EXPECT_FLOAT_EQ(v.checkboxSize,        p.checkboxSize);
    EXPECT_FLOAT_EQ(v.dropdownHeight,      p.dropdownHeight);
    EXPECT_FLOAT_EQ(v.keybindKeyMinWidth,  p.keybindKeyMinWidth);
}

// -- UIButton --

TEST(UIButton, DefaultStateAndDefaults)
{
    UIButton b;
    EXPECT_EQ(b.style, UIButtonStyle::DEFAULT);
    EXPECT_EQ(b.state, UIButtonState::NORMAL);
    EXPECT_FALSE(b.disabled);
    EXPECT_FALSE(b.small);
    EXPECT_FALSE(b.shortcut.present);
    EXPECT_TRUE(b.interactive);
}

TEST(UIButton, EffectiveHeightTracksSmallFlag)
{
    UIButton b;
    UITheme  t;
    b.theme = &t;
    EXPECT_FLOAT_EQ(b.effectiveHeight(), t.buttonHeight);
    b.small = true;
    EXPECT_FLOAT_EQ(b.effectiveHeight(), t.buttonHeightSmall);
}

TEST(UIButton, EffectiveHeightSafeWithoutTheme)
{
    UIButton b;
    EXPECT_FLOAT_EQ(b.effectiveHeight(), 56.0f);
    b.small = true;
    EXPECT_FLOAT_EQ(b.effectiveHeight(), 40.0f);
}

// -- UISlider --

TEST(UISlider, RatioClampsToZeroOne)
{
    UISlider s;
    s.minValue = 0.0f; s.maxValue = 100.0f;
    s.value = -5.0f;  EXPECT_FLOAT_EQ(s.ratio(), 0.0f);
    s.value = 50.0f;  EXPECT_FLOAT_EQ(s.ratio(), 0.5f);
    s.value = 200.0f; EXPECT_FLOAT_EQ(s.ratio(), 1.0f);
}

TEST(UISlider, RatioIsZeroForDegenerateRange)
{
    UISlider s;
    s.minValue = 5.0f;
    s.maxValue = 5.0f;  // max <= min
    s.value = 5.0f;
    EXPECT_FLOAT_EQ(s.ratio(), 0.0f);
}

// -- UICheckbox --

TEST(UICheckbox, DefaultIsUncheckedNotHovered)
{
    UICheckbox c;
    EXPECT_FALSE(c.checked);
    EXPECT_FALSE(c.hovered);
    EXPECT_TRUE(c.interactive);
}

// -- UIDropdown --

TEST(UIDropdown, CurrentLabelHandlesOutOfRange)
{
    UIDropdown d;
    d.options = {{"a", "Alpha"}, {"b", "Beta"}};
    d.selectedIndex = 0;
    EXPECT_EQ(d.currentLabel(), "Alpha");
    d.selectedIndex = 1;
    EXPECT_EQ(d.currentLabel(), "Beta");
    d.selectedIndex = -1;
    EXPECT_EQ(d.currentLabel(), "");
    d.selectedIndex = 99;
    EXPECT_EQ(d.currentLabel(), "");
}

TEST(UIDropdown, DefaultStateClosedNotHovered)
{
    UIDropdown d;
    EXPECT_FALSE(d.open);
    EXPECT_FALSE(d.hovered);
    EXPECT_EQ(d.selectedIndex, 0);
    EXPECT_TRUE(d.options.empty());
}

// -- UIKeybindRow --

TEST(UIKeybindRow, DefaultsAreNotListening)
{
    UIKeybindRow r;
    EXPECT_FALSE(r.listening);
    EXPECT_FALSE(r.hovered);
    EXPECT_TRUE(r.label.empty());
    EXPECT_TRUE(r.keyText.empty());
}
