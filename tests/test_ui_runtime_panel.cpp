// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_runtime_panel.cpp
/// @brief Phase 10 slice 12.5 — headless coverage for `UIRuntimePanel`
///        state: open/close, screen-push log capping, menu-preview
///        canvas rebuild, HUD-toggle read/write, HUD-toggle application
///        to a live `UISystem`.

#include <gtest/gtest.h>

#include "editor/panels/ui_runtime_panel.h"
#include "systems/ui_system.h"
#include "ui/game_screen.h"
#include "ui/ui_canvas.h"
#include "ui/ui_panel.h"
#include "ui/ui_theme.h"

#include <memory>

using namespace Vestige;

// -- Open / close --

TEST(UIRuntimePanel, DefaultsToClosed)
{
    UIRuntimePanel panel;
    EXPECT_FALSE(panel.isOpen());
}

TEST(UIRuntimePanel, ToggleFlipsOpenState)
{
    UIRuntimePanel panel;
    panel.toggleOpen();
    EXPECT_TRUE(panel.isOpen());
    panel.toggleOpen();
    EXPECT_FALSE(panel.isOpen());
}

// -- Screen-push log --

TEST(UIRuntimePanel, ScreenLogStartsEmpty)
{
    UIRuntimePanel panel;
    EXPECT_TRUE(panel.screenLog().empty());
}

TEST(UIRuntimePanel, ScreenLogRecordsTransitions)
{
    UIRuntimePanel panel;
    panel.recordScreenTransition(GameScreen::None,
                                 GameScreen::MainMenu,
                                 GameScreenIntent::OpenMainMenu);
    panel.recordScreenTransition(GameScreen::MainMenu,
                                 GameScreen::Loading,
                                 GameScreenIntent::NewWalkthrough);
    ASSERT_EQ(panel.screenLog().size(), 2u);
    EXPECT_EQ(panel.screenLog().front().from, GameScreen::None);
    EXPECT_EQ(panel.screenLog().front().to,   GameScreen::MainMenu);
    EXPECT_EQ(panel.screenLog().back().intent, GameScreenIntent::NewWalkthrough);
}

TEST(UIRuntimePanel, ScreenLogCapsAtCapacity)
{
    UIRuntimePanel panel;
    for (std::size_t i = 0; i < UIRuntimePanel::SCREEN_LOG_CAPACITY + 5; ++i)
    {
        panel.recordScreenTransition(GameScreen::MainMenu,
                                     GameScreen::Paused,
                                     GameScreenIntent::Pause);
    }
    EXPECT_EQ(panel.screenLog().size(), UIRuntimePanel::SCREEN_LOG_CAPACITY);
}

TEST(UIRuntimePanel, ScreenLogClearDropsEverything)
{
    UIRuntimePanel panel;
    panel.recordScreenTransition(GameScreen::MainMenu,
                                 GameScreen::Paused,
                                 GameScreenIntent::Pause);
    panel.clearScreenLog();
    EXPECT_TRUE(panel.screenLog().empty());
}

// -- Menu preview --

TEST(UIRuntimePanel, MenuPreviewDefaultsToMainMenu)
{
    UIRuntimePanel panel;
    EXPECT_EQ(panel.menuPreview(), UIRuntimePanelMenu::MainMenu);
    EXPECT_EQ(panel.previewCanvas().getElementCount(), 0u)
        << "Preview canvas stays empty until refreshMenuPreview is called";
}

TEST(UIRuntimePanel, RefreshMenuPreviewBuildsMainMenu)
{
    UIRuntimePanel panel;
    UITheme theme = UITheme::defaultTheme();
    panel.refreshMenuPreview(theme, nullptr);
    EXPECT_GT(panel.previewCanvas().getElementCount(), 10u);
}

TEST(UIRuntimePanel, SetMenuPreviewSwitchesAndRebuildReflects)
{
    UIRuntimePanel panel;
    UITheme theme = UITheme::defaultTheme();
    panel.refreshMenuPreview(theme, nullptr);
    const size_t mainCount = panel.previewCanvas().getElementCount();

    panel.setMenuPreview(UIRuntimePanelMenu::Paused);
    panel.refreshMenuPreview(theme, nullptr);
    EXPECT_EQ(panel.menuPreview(), UIRuntimePanelMenu::Paused);
    EXPECT_GT(panel.previewCanvas().getElementCount(), 15u)
        << "Pause menu prefab populates more elements than the empty canvas";

    panel.setMenuPreview(UIRuntimePanelMenu::Settings);
    panel.refreshMenuPreview(theme, nullptr);
    EXPECT_EQ(panel.menuPreview(), UIRuntimePanelMenu::Settings);
    EXPECT_GT(panel.previewCanvas().getElementCount(), 0u);

    // Round-trip back to main menu to confirm the canvas is cleared and
    // rebuilt rather than appended to.
    panel.setMenuPreview(UIRuntimePanelMenu::MainMenu);
    panel.refreshMenuPreview(theme, nullptr);
    EXPECT_EQ(panel.previewCanvas().getElementCount(), mainCount);
}

// -- HUD element toggles --

TEST(UIRuntimePanel, HudElementsDefaultToVisible)
{
    UIRuntimePanel panel;
    EXPECT_TRUE(panel.isHudElementVisible(UIRuntimePanel::HudElement::Crosshair));
    EXPECT_TRUE(panel.isHudElementVisible(UIRuntimePanel::HudElement::FpsCounter));
    EXPECT_TRUE(panel.isHudElementVisible(UIRuntimePanel::HudElement::InteractionAnchor));
    EXPECT_TRUE(panel.isHudElementVisible(UIRuntimePanel::HudElement::NotificationStack));
}

TEST(UIRuntimePanel, HudElementToggleRoundTrips)
{
    UIRuntimePanel panel;
    panel.setHudElementVisible(UIRuntimePanel::HudElement::FpsCounter, false);
    EXPECT_FALSE(panel.isHudElementVisible(UIRuntimePanel::HudElement::FpsCounter));
    EXPECT_TRUE(panel.isHudElementVisible(UIRuntimePanel::HudElement::Crosshair));
}

TEST(UIRuntimePanel, ApplyHudTogglesWritesToLivePlayingCanvas)
{
    UISystem ui;
    ui.setRootScreen(GameScreen::Playing);
    // The default HUD builder lands 4 elements on the root canvas.
    ASSERT_EQ(ui.getCanvas().getElementCount(), 4u);

    UIRuntimePanel panel;
    panel.setHudElementVisible(UIRuntimePanel::HudElement::Crosshair, false);
    panel.setHudElementVisible(UIRuntimePanel::HudElement::FpsCounter, false);

    const std::size_t written = panel.applyHudTogglesTo(ui);
    EXPECT_EQ(written, 4u);

    EXPECT_FALSE(ui.getCanvas().getElementAt(0)->visible)
        << "Crosshair visibility must follow the panel toggle.";
    EXPECT_FALSE(ui.getCanvas().getElementAt(1)->visible)
        << "FPS counter visibility must follow the panel toggle.";
    EXPECT_TRUE(ui.getCanvas().getElementAt(2)->visible);
    EXPECT_TRUE(ui.getCanvas().getElementAt(3)->visible);
}

TEST(UIRuntimePanel, ApplyHudTogglesNoOpWhenRootIsNotPlaying)
{
    UISystem ui;
    ui.setRootScreen(GameScreen::MainMenu);  // HUD prefab not active.
    UIRuntimePanel panel;
    panel.setHudElementVisible(UIRuntimePanel::HudElement::Crosshair, false);
    const std::size_t written = panel.applyHudTogglesTo(ui);
    EXPECT_EQ(written, 0u)
        << "HUD toggles must not clobber a non-HUD screen's canvas.";
}

TEST(UIRuntimePanel, ApplyHudTogglesHandlesShorterCanvasViaCustomBuilder)
{
    // Guards against a future prefab change: if someone shrinks
    // `buildDefaultHud` to emit only 2 elements, the panel must still
    // apply what it can without dereferencing beyond the canvas. Exercise
    // the clamp by registering a custom Playing builder that drops 2
    // elements instead of 4.
    UISystem ui;
    ui.setScreenBuilder(GameScreen::Playing,
        [](UICanvas& canvas, const UITheme& /*t*/, TextRenderer* /*tr*/,
           UISystem& /*u*/)
        {
            auto a = std::make_unique<UIPanel>();
            auto b = std::make_unique<UIPanel>();
            canvas.addElement(std::move(a));
            canvas.addElement(std::move(b));
        });
    ui.setRootScreen(GameScreen::Playing);
    ASSERT_EQ(ui.getCanvas().getElementCount(), 2u);

    UIRuntimePanel panel;
    panel.setHudElementVisible(UIRuntimePanel::HudElement::Crosshair, false);
    panel.setHudElementVisible(UIRuntimePanel::HudElement::FpsCounter, false);

    const std::size_t written = panel.applyHudTogglesTo(ui);
    EXPECT_EQ(written, 2u)
        << "applyHudTogglesTo clamps to min(canvasCount, HUD_ELEMENT_COUNT).";
    EXPECT_FALSE(ui.getCanvas().getElementAt(0)->visible);
    EXPECT_FALSE(ui.getCanvas().getElementAt(1)->visible);
}
