// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_menu_prefabs.cpp
/// @brief Validates the Phase 9C menu prefab factories populate canvases
///        with the expected element count and structural shape.

#include <gtest/gtest.h>

#include "ui/menu_prefabs.h"
#include "ui/ui_button.h"
#include "ui/ui_canvas.h"
#include "ui/ui_label.h"
#include "ui/ui_panel.h"
#include "ui/ui_theme.h"
#include "systems/ui_system.h"

#include <string>
#include <vector>

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

// 3D_E-0785 — at a larger interface scale the sidebar's category buttons were
// drawn at the theme's button height (84 px at 1.5x), so a hover fill spilled
// into the row below. Rows stay a fixed step apart, so the button keeps it.
TEST(MenuPrefabs, SettingsCategoryButtonsFitTheirRowsAtAnyScale)
{
    for (float scale : {1.0f, 1.5f, 2.0f})
    {
        SCOPED_TRACE(scale);
        UICanvas canvas;
        UISystem ui;
        const UITheme theme = UITheme::defaultTheme().withScale(scale);
        SettingsMenuActions actions;
        actions.categories = {"ui.settings.category.display", "ui.settings.category.audio"};
        buildSettingsMenu(canvas, theme, nullptr, ui, actions);

        std::vector<const UIButton*> rows;
        for (size_t i = 0; i < canvas.getElementCount(); ++i)
        {
            const auto* b = dynamic_cast<const UIButton*>(canvas.getElementAt(i));
            if (b != nullptr && b->label.empty())  // category buttons carry no caption
            {
                rows.push_back(b);
            }
        }
        ASSERT_EQ(rows.size(), 2u);
        const float step = rows[1]->position.y - rows[0]->position.y;
        for (const UIButton* b : rows)
        {
            EXPECT_LE(b->drawnHeight(), step) << "drawn " << b->drawnHeight()
                                              << " px in a " << step << " px row";
        }
    }
}

// 3D_E-0760 — Settings opens over the pause menu. A see-through Settings panel
// let the pause menu's title and buttons show faintly behind it.
TEST(MenuPrefabs, SettingsModalPanelIsOpaque)
{
    for (const UITheme& theme : {UITheme::defaultTheme(), UITheme::plumbline()})
    {
        UICanvas canvas;
        buildSettingsMenu(canvas, theme, nullptr);
        const UIPanel* modal = nullptr;
        for (size_t i = 0; i < canvas.getElementCount(); ++i)
        {
            const auto* panel = dynamic_cast<const UIPanel*>(canvas.getElementAt(i));
            if (panel != nullptr && panel->position == glm::vec2(120.0f, 80.0f))
            {
                modal = panel;
                break;
            }
        }
        ASSERT_NE(modal, nullptr) << "no modal panel at (120, 80)";
        EXPECT_FLOAT_EQ(modal->backgroundColor.a, 1.0f);
    }
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

// -- 3D_E-0744: no made-up data on the menus --

namespace
{

std::vector<std::string> labelTexts(const UICanvas& canvas)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < canvas.getElementCount(); ++i)
    {
        if (const auto* label = dynamic_cast<const UILabel*>(canvas.getElementAt(i)))
        {
            out.push_back(label->text);
        }
    }
    return out;
}

}  // namespace

TEST(MenuPrefabs, MenusShowTheRealVersionAndNoPlaceholderData)
{
    UITheme theme = UITheme::defaultTheme();
    UICanvas main;
    buildMainMenu(main, theme, nullptr);
    UICanvas pause;
    buildPauseMenu(pause, theme, nullptr);

    bool versionShown = false;
    for (const UICanvas* canvas : {&main, &pause})
    {
        for (const std::string& text : labelTexts(*canvas))
        {
            if (text.find(VESTIGE_ENGINE_VERSION) != std::string::npos)
            {
                versionShown = true;
            }
            for (const char* fake : {"0.6.2", "14:22", "14:19", "Pillar 07", "PILLAR 07", "SLOT 03"})
            {
                EXPECT_EQ(text.find(fake), std::string::npos)
                    << "menu label shows placeholder data: " << text;
            }
        }
    }
    EXPECT_TRUE(versionShown) << "the main menu does not show the engine's version";
}
