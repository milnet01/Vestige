// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_menu_prefabs.cpp
/// @brief Validates the Phase 9C menu prefab factories populate canvases
///        with the expected element count and structural shape.

#include <gtest/gtest.h>

#include "ui/menu_prefabs.h"
#include "ui/ui_canvas.h"
#include "ui/ui_theme.h"

using namespace Vestige;

// -- buildMainMenu --

TEST(MenuPrefabs, MainMenuPopulatesCanvas)
{
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildMainMenu(canvas, theme, /*textRenderer=*/nullptr);

    // Sanity bound — main menu has chrome + wordmark + 5 buttons + continue
    // card + footer. Lower bound matches the design's element count; upper
    // bound catches accidental duplication.
    EXPECT_GT(canvas.getElementCount(), 10u);
    EXPECT_LT(canvas.getElementCount(), 30u);
}

TEST(MenuPrefabs, MainMenuIsCleanCanvas)
{
    // Calling builder twice on the same canvas duplicates content — caller's
    // responsibility to call canvas.clear() first if rebuilding. Verify each
    // call adds the full set of elements.
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildMainMenu(canvas, theme, nullptr);
    const size_t firstCount = canvas.getElementCount();
    buildMainMenu(canvas, theme, nullptr);
    EXPECT_EQ(canvas.getElementCount(), firstCount * 2);
}

// -- buildPauseMenu --

TEST(MenuPrefabs, PauseMenuPopulatesCanvas)
{
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildPauseMenu(canvas, theme, nullptr);

    // Pause has scrim + caption + headline + 8 corner-bracket strips +
    // panel + 7 buttons + footer labels. Bound generously.
    EXPECT_GT(canvas.getElementCount(), 15u);
    EXPECT_LT(canvas.getElementCount(), 40u);
}

TEST(MenuPrefabs, PauseMenuWorksOnPlumblineRegister)
{
    UICanvas canvas;
    UITheme  theme = UITheme::plumbline();
    buildPauseMenu(canvas, theme, nullptr);
    // Plumbline is structurally identical to Vellum; element count must match.
    UICanvas vellum;
    buildPauseMenu(vellum, UITheme::defaultTheme(), nullptr);
    EXPECT_EQ(canvas.getElementCount(), vellum.getElementCount());
}

// -- buildSettingsMenu --

TEST(MenuPrefabs, SettingsMenuPopulatesChrome)
{
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildSettingsMenu(canvas, theme, nullptr);

    // Settings ships only the chrome (no per-category controls). Backdrop +
    // modal + header (3) + sidebar rule + 5 categories (with first highlighted
    // adding 2 extras) + footer (rule + label + 3 buttons).
    EXPECT_GT(canvas.getElementCount(), 12u);
    EXPECT_LT(canvas.getElementCount(), 30u);
}

TEST(MenuPrefabs, AllPrefabsAreSafeWithoutTextRenderer)
{
    // Builders must not crash when textRenderer is null (text elements
    // simply skip the draw call at render time). This lets game projects
    // construct prefabs at startup before the renderer is wired.
    UICanvas main, pause, settings;
    UITheme  theme = UITheme::defaultTheme();
    buildMainMenu(main, theme, nullptr);
    buildPauseMenu(pause, theme, nullptr);
    buildSettingsMenu(settings, theme, nullptr);
    SUCCEED();
}
