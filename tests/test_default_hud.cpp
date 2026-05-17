// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_default_hud.cpp
/// @brief Phase 10 slice 12.4 — `buildDefaultHud` prefab shape and
///        anchor correctness.

#include <gtest/gtest.h>

#include "ui/menu_prefabs.h"
#include "ui/ui_canvas.h"
#include "ui/ui_crosshair.h"
#include "ui/ui_fps_counter.h"
#include "ui/ui_notification_toast.h"
#include "ui/ui_panel.h"
#include "ui/ui_theme.h"
#include "systems/ui_system.h"

using namespace Vestige;

// /test-audit 2026-05-17 Ts19-F1: every `DefaultHudPrefab` test below
// constructed the trio `UICanvas / UITheme / UISystem` and called
// `populate`. Centralised into a fixture so the build call lives in one
// `SetUp` instead of being re-invoked at the top of every test body.
class DefaultHudPrefabTest : public ::testing::Test
{
protected:
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    UISystem ui;

    void SetUp() override
    {
        // Matches the original `populate` helper — the `UISystem` ref is
        // required by the signature; the prefab doesn't currently wire
        // signals through it.
        buildDefaultHud(canvas, theme, /*textRenderer=*/nullptr, ui);
    }
};

// -- Element count / ordering --

TEST_F(DefaultHudPrefabTest, PopulatesFourRootElements)
{
    EXPECT_EQ(canvas.getElementCount(), 4u)
        << "HUD = crosshair + FPS + interaction anchor + notification stack";
}

TEST_F(DefaultHudPrefabTest, ElementOrderMatchesDesign)
{
    ASSERT_EQ(canvas.getElementCount(), 4u);
    EXPECT_NE(dynamic_cast<UICrosshair*>(canvas.getElementAt(0)), nullptr);
    EXPECT_NE(dynamic_cast<UIFpsCounter*>(canvas.getElementAt(1)), nullptr);
    EXPECT_NE(dynamic_cast<UIPanel*>(canvas.getElementAt(2)), nullptr)
        << "Interaction-prompt anchor is a transparent UIPanel";
    EXPECT_NE(dynamic_cast<UIPanel*>(canvas.getElementAt(3)), nullptr)
        << "Notification stack is a UIPanel container";
}

// -- Anchor correctness --

TEST_F(DefaultHudPrefabTest, CrosshairAnchorsAtCenter)
{
    auto* crosshair = dynamic_cast<UICrosshair*>(canvas.getElementAt(0));
    ASSERT_NE(crosshair, nullptr);
    EXPECT_EQ(crosshair->anchor, Anchor::CENTER);
    EXPECT_FLOAT_EQ(crosshair->armLength, theme.crosshairLength);
    EXPECT_FLOAT_EQ(crosshair->thickness, theme.crosshairThickness);
    EXPECT_EQ(crosshair->color, theme.crosshair);
}

TEST_F(DefaultHudPrefabTest, FpsCounterAnchorsTopLeftAndHiddenByDefault)
{
    auto* fps = dynamic_cast<UIFpsCounter*>(canvas.getElementAt(1));
    ASSERT_NE(fps, nullptr);
    EXPECT_EQ(fps->anchor, Anchor::TOP_LEFT);
    EXPECT_FALSE(fps->visible) << "FPS counter is debug-only; hidden by default.";
}

TEST_F(DefaultHudPrefabTest, InteractionPromptAnchorBottomCenter)
{
    auto* slot = dynamic_cast<UIPanel*>(canvas.getElementAt(2));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->anchor, Anchor::BOTTOM_CENTER);
    EXPECT_FLOAT_EQ(slot->backgroundColor.a, 0.0f)
        << "Anchor slot must be fully transparent — it's a layout holder.";
}

TEST_F(DefaultHudPrefabTest, NotificationStackAnchorsTopRightWithThreeSlots)
{
    auto* stack = dynamic_cast<UIPanel*>(canvas.getElementAt(3));
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->anchor, Anchor::TOP_RIGHT);
    EXPECT_FLOAT_EQ(stack->backgroundColor.a, 0.0f)
        << "Stack container is an invisible holder; toasts paint themselves.";
    EXPECT_EQ(stack->getChildCount(), NotificationQueue::DEFAULT_CAPACITY);
}

// -- Accessibility walk over an unpopulated notification stack --

// Slice 18 Ts1 cleanup: renamed from `NotificationSlotsStartInvisible`
// — the body never reads slot alpha, just confirms `collectAccessible`
// doesn't crash on the prefab. Slot-alpha verification needs an
// observable the panel doesn't currently expose.
TEST_F(DefaultHudPrefabTest, NotificationCanvasWalkDoesNotCrashOnUnpopulatedHud)
{
    const std::vector<UIAccessibilitySnapshot> snaps = canvas.collectAccessible();
    (void)snaps;
}

// -- Clean canvas semantics --

TEST_F(DefaultHudPrefabTest, RebuildOnSameCanvasAppends)
{
    // Matches the existing menu_prefabs convention — caller clears the
    // canvas before rebuilding. Consistency with buildMainMenu. SetUp
    // already populated once; this rebuilds a second time on top.
    const size_t first = canvas.getElementCount();
    buildDefaultHud(canvas, theme, /*textRenderer=*/nullptr, ui);
    EXPECT_EQ(canvas.getElementCount(), first * 2);
}

// -- High-contrast palette survival --

TEST(DefaultHudPrefab, PicksUpHighContrastThemeColours)
{
    // Kept as TEST() rather than TEST_F: this case uses the high-contrast
    // theme variant instead of the fixture's default theme.
    UICanvas canvas;
    UITheme hcTheme = UITheme::defaultTheme().withHighContrast();
    UISystem ui;
    buildDefaultHud(canvas, hcTheme, /*textRenderer=*/nullptr, ui);

    auto* crosshair = dynamic_cast<UICrosshair*>(canvas.getElementAt(0));
    ASSERT_NE(crosshair, nullptr);
    EXPECT_EQ(crosshair->color, hcTheme.crosshair)
        << "High-contrast crosshair colour must survive the build.";
}

// -- UISystem notification queue wiring --

TEST(UISystemNotifications, QueueStartsEmpty)
{
    UISystem ui;
    EXPECT_TRUE(ui.getNotifications().empty());
}

TEST(UISystemNotifications, UpdateAdvancesQueue)
{
    UISystem ui;
    Notification n;
    n.title = "Saved";
    n.durationSeconds = 1.0f;
    ui.getNotifications().push(n);

    ASSERT_EQ(ui.getNotifications().size(), 1u);
    // Snap mode (transitionDuration = 0 by way of reduced-motion would be
    // simplest, but default theme has 0.14s; advance past total duration
    // to guarantee expiry in one tick regardless of fade length).
    ui.update(2.0f);
    EXPECT_TRUE(ui.getNotifications().empty());
}

TEST(UISystemNotifications, ScreenBuilderForPlayingIsDefaultHud)
{
    // `Playing` now has a built-in default builder — setting the root
    // screen should populate the canvas with 4 HUD elements without
    // the caller registering a custom builder.
    UISystem ui;
    ui.setRootScreen(GameScreen::Playing);
    EXPECT_EQ(ui.getCanvas().getElementCount(), 4u);
}
