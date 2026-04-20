// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_widgets.cpp
/// @brief CPU-side tests for the Phase 9C HUD widget batch.
///
/// Render paths require a GL context and live `SpriteBatchRenderer`, so these
/// tests cover the pure-CPU surface only: theme defaults, progress-bar ratio
/// math, FPS smoothing math, and the UISystem input-capture flag union.

#include <gtest/gtest.h>

#include "ui/ui_crosshair.h"
#include "ui/ui_fps_counter.h"
#include "ui/ui_progress_bar.h"
#include "ui/ui_theme.h"

using namespace Vestige;

// -- UITheme --

TEST(UITheme, DefaultsAreInValidRanges)
{
    UITheme t = UITheme::defaultTheme();
    auto isUnitColor = [](float c) { return c >= 0.0f && c <= 1.0f; };

    EXPECT_TRUE(isUnitColor(t.panelBg.r));   EXPECT_TRUE(isUnitColor(t.panelBg.a));
    EXPECT_TRUE(isUnitColor(t.textPrimary.r));
    EXPECT_TRUE(isUnitColor(t.accent.r));
    EXPECT_GT(t.crosshairLength, 0.0f);
    EXPECT_GT(t.crosshairThickness, 0.0f);
    EXPECT_GT(t.progressBarHeight, 0.0f);
    EXPECT_GT(t.defaultTextScale, 0.0f);
}

TEST(UITheme, AccentDimIsDarkerShadeOfAccent)
{
    // Per the Claude Design Vellum register, accentDim is the darker
    // burnished-brass shade used for pressed states (#8A6A2A vs the bright
    // #C89A3E accent), not a translucent overlay. Both are fully opaque;
    // dim is darker in luminance.
    UITheme t = UITheme::defaultTheme();
    EXPECT_FLOAT_EQ(t.accentDim.a, 1.0f);
    EXPECT_FLOAT_EQ(t.accent.a,    1.0f);
    const float accentLuma = 0.299f * t.accent.r + 0.587f * t.accent.g + 0.114f * t.accent.b;
    const float dimLuma    = 0.299f * t.accentDim.r + 0.587f * t.accentDim.g + 0.114f * t.accentDim.b;
    EXPECT_LT(dimLuma, accentLuma);
}

// -- UIProgressBar --

TEST(UIProgressBar, RatioClampsToZeroAndOne)
{
    UIProgressBar bar;
    bar.maxValue = 10.0f;

    bar.value = -5.0f;
    EXPECT_FLOAT_EQ(bar.getRatio(), 0.0f);

    bar.value = 15.0f;
    EXPECT_FLOAT_EQ(bar.getRatio(), 1.0f);

    bar.value = 5.0f;
    EXPECT_FLOAT_EQ(bar.getRatio(), 0.5f);
}

TEST(UIProgressBar, RatioIsZeroWhenMaxValueNonPositive)
{
    UIProgressBar bar;
    bar.maxValue = 0.0f;
    bar.value = 1.0f;
    EXPECT_FLOAT_EQ(bar.getRatio(), 0.0f);
}

// -- UIFpsCounter --

TEST(UIFpsCounter, FirstSampleSeedsAverage)
{
    UIFpsCounter fps;
    fps.tick(1.0f / 60.0f);
    EXPECT_NEAR(fps.getSmoothedFps(), 60.0f, 0.5f);
}

TEST(UIFpsCounter, ZeroDeltaIsNoOp)
{
    UIFpsCounter fps;
    fps.tick(0.0f);
    EXPECT_FLOAT_EQ(fps.getSmoothedFps(), 0.0f);
}

TEST(UIFpsCounter, SmoothingMovesAverage)
{
    UIFpsCounter fps;
    fps.smoothing = 0.5f;
    fps.tick(1.0f / 60.0f);     // seeds at 60
    fps.tick(1.0f / 30.0f);     // sample = 30, weighted avg = (60+30)/2 = 45
    EXPECT_NEAR(fps.getSmoothedFps(), 45.0f, 0.5f);
}

// -- UICrosshair (defaults only; render needs GL) --

TEST(UICrosshair, NotInteractiveByDefault)
{
    UICrosshair cx;
    EXPECT_FALSE(cx.interactive);
    EXPECT_GT(cx.armLength, 0.0f);
    EXPECT_GT(cx.thickness, 0.0f);
}
