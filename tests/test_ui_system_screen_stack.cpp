// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_system_screen_stack.cpp
/// @brief Slice 12.2 coverage for the `UISystem` screen stack, signal
///        emission, `applyIntent` routing, `setScreenBuilder` override,
///        and `menu_prefabs` onClick wiring. Tests run without a GL
///        context — `UISystem` is constructed but not `initialize()`d,
///        so the sprite batch is inert and only the CPU-side state
///        machinery is exercised.

#include <gtest/gtest.h>

#include "systems/ui_system.h"
#include "ui/game_screen.h"
#include "ui/menu_prefabs.h"
#include "ui/ui_button.h"
#include "ui/ui_canvas.h"

#include <string>
#include <vector>

using namespace Vestige;

namespace
{

// Locates the first UIButton with a label matching @a label inside @a canvas.
// Returns nullptr if none is found. Slice 12.2 wires menu-prefab buttons by
// label, so tests look up by label rather than relying on ordering.
UIButton* findButtonByLabel(UICanvas& canvas, const std::string& label)
{
    for (size_t i = 0; i < canvas.getElementCount(); ++i)
    {
        auto* el = canvas.getElementAt(i);
        if (auto* btn = dynamic_cast<UIButton*>(el))
        {
            if (btn->label == label)
            {
                return btn;
            }
        }
    }
    return nullptr;
}

} // namespace

// -- Defaults ---------------------------------------------------------------

TEST(UISystemScreenStack, DefaultRootIsNone)
{
    UISystem sys;
    EXPECT_EQ(sys.getRootScreen(), GameScreen::None);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::None);
    EXPECT_EQ(sys.getCurrentScreen(), GameScreen::None);
    EXPECT_FALSE(sys.isModalCapture());
}

// -- setRootScreen ----------------------------------------------------------

TEST(UISystemScreenStack, SetRootScreenEmitsSignal)
{
    UISystem sys;
    std::vector<GameScreen> observed;
    sys.onRootScreenChanged.connect([&](GameScreen s) { observed.push_back(s); });

    sys.setRootScreen(GameScreen::MainMenu);
    ASSERT_EQ(observed.size(), 1u);
    EXPECT_EQ(observed[0], GameScreen::MainMenu);
    EXPECT_EQ(sys.getRootScreen(), GameScreen::MainMenu);
}

TEST(UISystemScreenStack, SetRootScreenRebuildsCanvas)
{
    UISystem sys;
    EXPECT_EQ(sys.getCanvas().getElementCount(), 0u);

    sys.setRootScreen(GameScreen::MainMenu);
    // MainMenu prefab populates >10 elements (chrome + wordmark + 5 buttons +
    // continue card + footer). Loose lower bound catches a silent no-op.
    EXPECT_GT(sys.getCanvas().getElementCount(), 10u);

    // Switching screen clears + rebuilds — count changes, capture drops.
    sys.setRootScreen(GameScreen::None);
    EXPECT_EQ(sys.getCanvas().getElementCount(), 0u);
    EXPECT_FALSE(sys.isModalCapture());
}

TEST(UISystemScreenStack, SetRootScreenClearsModalStack)
{
    UISystem sys;
    sys.setRootScreen(GameScreen::MainMenu);
    sys.pushModalScreen(GameScreen::Settings);
    ASSERT_EQ(sys.getTopModalScreen(), GameScreen::Settings);
    ASSERT_TRUE(sys.isModalCapture());

    sys.setRootScreen(GameScreen::Playing);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::None);
    EXPECT_EQ(sys.getModalCanvas().getElementCount(), 0u);
    EXPECT_FALSE(sys.isModalCapture());
}

// -- pushModalScreen / popModalScreen ---------------------------------------

TEST(UISystemScreenStack, PushModalEmitsSignalAndSetsCapture)
{
    UISystem sys;
    sys.setRootScreen(GameScreen::MainMenu);

    std::vector<GameScreen> pushed;
    sys.onModalPushed.connect([&](GameScreen s) { pushed.push_back(s); });

    sys.pushModalScreen(GameScreen::Settings);
    ASSERT_EQ(pushed.size(), 1u);
    EXPECT_EQ(pushed[0], GameScreen::Settings);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::Settings);
    EXPECT_EQ(sys.getCurrentScreen(), GameScreen::Settings);
    EXPECT_TRUE(sys.isModalCapture());
    EXPECT_GT(sys.getModalCanvas().getElementCount(), 0u);
}

TEST(UISystemScreenStack, PopModalEmitsSignalAndClearsCapture)
{
    UISystem sys;
    sys.setRootScreen(GameScreen::MainMenu);
    sys.pushModalScreen(GameScreen::Settings);

    std::vector<GameScreen> popped;
    sys.onModalPopped.connect([&](GameScreen s) { popped.push_back(s); });

    sys.popModalScreen();
    ASSERT_EQ(popped.size(), 1u);
    EXPECT_EQ(popped[0], GameScreen::Settings);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::None);
    EXPECT_EQ(sys.getCurrentScreen(), GameScreen::MainMenu);
    EXPECT_FALSE(sys.isModalCapture());
    EXPECT_EQ(sys.getModalCanvas().getElementCount(), 0u);
}

TEST(UISystemScreenStack, PopOnEmptyStackIsNoop)
{
    UISystem sys;
    sys.setRootScreen(GameScreen::MainMenu);

    bool fired = false;
    sys.onModalPopped.connect([&](GameScreen) { fired = true; });

    sys.popModalScreen();
    EXPECT_FALSE(fired);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::None);
}

// -- applyIntent routing ----------------------------------------------------

TEST(UISystemScreenStack, ApplyIntentPlayingPauseGoesToPaused)
{
    UISystem sys;
    sys.setRootScreen(GameScreen::Playing);
    sys.applyIntent(GameScreenIntent::Pause);
    EXPECT_EQ(sys.getRootScreen(), GameScreen::Paused);
}

TEST(UISystemScreenStack, ApplyIntentPausedResumeGoesToPlaying)
{
    UISystem sys;
    sys.setRootScreen(GameScreen::Paused);
    sys.applyIntent(GameScreenIntent::Resume);
    EXPECT_EQ(sys.getRootScreen(), GameScreen::Playing);
}

TEST(UISystemScreenStack, ApplyIntentOpenSettingsPushesModal)
{
    UISystem sys;
    sys.setRootScreen(GameScreen::MainMenu);

    bool rootChanged = false;
    sys.onRootScreenChanged.connect([&](GameScreen) { rootChanged = true; });

    sys.applyIntent(GameScreenIntent::OpenSettings);
    // Root stays MainMenu; a Settings modal stacks on top.
    EXPECT_EQ(sys.getRootScreen(), GameScreen::MainMenu);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::Settings);
    EXPECT_EQ(sys.getCurrentScreen(), GameScreen::Settings);
    EXPECT_FALSE(rootChanged) << "OpenSettings must not change the root";
    EXPECT_TRUE(sys.isModalCapture());
}

TEST(UISystemScreenStack, ApplyIntentCloseSettingsPopsModalPreservingRoot)
{
    // Opening Settings from Paused, then closing, must land back on Paused
    // — not MainMenu (the pure-function fallback). This is the key
    // reason UISystem owns a modal stack on top of the pure state machine.
    UISystem sys;
    sys.setRootScreen(GameScreen::Paused);
    sys.applyIntent(GameScreenIntent::OpenSettings);
    ASSERT_EQ(sys.getCurrentScreen(), GameScreen::Settings);

    sys.applyIntent(GameScreenIntent::CloseSettings);
    EXPECT_EQ(sys.getRootScreen(), GameScreen::Paused);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::None);
    EXPECT_EQ(sys.getCurrentScreen(), GameScreen::Paused);
    EXPECT_FALSE(sys.isModalCapture());
}

TEST(UISystemScreenStack, ApplyIntentRejectedIntentIsNoop)
{
    // Firing Resume while Playing is invalid per the pure state machine.
    // applyIntent must not emit signals or mutate state.
    UISystem sys;
    sys.setRootScreen(GameScreen::Playing);

    int rootSignalCount = 0;
    sys.onRootScreenChanged.connect([&](GameScreen) { ++rootSignalCount; });
    int modalSignalCount = 0;
    sys.onModalPushed.connect([&](GameScreen) { ++modalSignalCount; });

    sys.applyIntent(GameScreenIntent::Resume);
    EXPECT_EQ(sys.getRootScreen(), GameScreen::Playing);
    EXPECT_EQ(rootSignalCount, 0);
    EXPECT_EQ(modalSignalCount, 0);
}

TEST(UISystemScreenStack, ApplyIntentPlayingQuitToMainSwapsRoot)
{
    UISystem sys;
    sys.setRootScreen(GameScreen::Playing);
    sys.applyIntent(GameScreenIntent::QuitToMain);
    EXPECT_EQ(sys.getRootScreen(), GameScreen::MainMenu);
}

// -- setScreenBuilder override ---------------------------------------------

TEST(UISystemScreenStack, SetScreenBuilderOverridesDefault)
{
    // A custom builder for Paused should be invoked instead of the default
    // buildPauseMenu prefab. The canvas it produces must match what the
    // custom builder put there (a single label), not the pause prefab's
    // ~20-element panel.
    UISystem sys;
    int callCount = 0;
    sys.setScreenBuilder(GameScreen::Paused,
        [&](UICanvas& canvas, const UITheme&, TextRenderer*, UISystem&)
        {
            ++callCount;
            // Minimal placeholder content — just needs to be non-empty so
            // the canvas can be distinguished from the default prefab.
            canvas.clear();
            // Sentinel element: single panel. The default pause prefab
            // adds dozens — count mismatch proves the override ran.
        });

    sys.setRootScreen(GameScreen::Paused);
    EXPECT_EQ(callCount, 1);
    // Override produced zero elements (it only cleared). Default prefab
    // would have produced >10 — this asserts the override actually ran.
    EXPECT_EQ(sys.getCanvas().getElementCount(), 0u);
}

TEST(UISystemScreenStack, SetScreenBuilderEmptyClearsOverride)
{
    UISystem sys;
    int customCalls = 0;
    sys.setScreenBuilder(GameScreen::Paused,
        [&](UICanvas&, const UITheme&, TextRenderer*, UISystem&) { ++customCalls; });

    // Clearing with an empty callable restores the built-in default.
    sys.setScreenBuilder(GameScreen::Paused, {});
    sys.setRootScreen(GameScreen::Paused);
    EXPECT_EQ(customCalls, 0);
    EXPECT_GT(sys.getCanvas().getElementCount(), 10u)
        << "Default buildPauseMenu should have run after clearing override";
}

// -- menu_prefabs signal wiring --------------------------------------------

TEST(MenuPrefabs, MainMenuButtonsFireIntentsViaUISystem)
{
    UISystem sys;
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildMainMenu(canvas, theme, /*textRenderer=*/nullptr, sys);

    // Sanity: setting the root to None first so we can observe MainMenu →
    // Loading (NewWalkthrough intent) transitions via the button click.
    // The build above is a fresh canvas — it doesn't affect sys state.
    sys.setRootScreen(GameScreen::MainMenu);
    ASSERT_EQ(sys.getRootScreen(), GameScreen::MainMenu);

    auto* newWalk = findButtonByLabel(canvas, "New Walkthrough");
    ASSERT_NE(newWalk, nullptr);
    EXPECT_TRUE(newWalk->interactive);
    newWalk->onClick.emit();
    EXPECT_EQ(sys.getRootScreen(), GameScreen::Loading);
}

TEST(MenuPrefabs, MainMenuSettingsButtonOpensModal)
{
    UISystem sys;
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildMainMenu(canvas, theme, nullptr, sys);

    sys.setRootScreen(GameScreen::MainMenu);
    auto* settingsBtn = findButtonByLabel(canvas, "Settings");
    ASSERT_NE(settingsBtn, nullptr);

    settingsBtn->onClick.emit();
    EXPECT_EQ(sys.getRootScreen(), GameScreen::MainMenu);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::Settings);
}

TEST(MenuPrefabs, PauseMenuResumeButtonFiresResume)
{
    UISystem sys;
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildPauseMenu(canvas, theme, nullptr, sys);

    sys.setRootScreen(GameScreen::Paused);
    auto* resumeBtn = findButtonByLabel(canvas, "Resume");
    ASSERT_NE(resumeBtn, nullptr);

    resumeBtn->onClick.emit();
    EXPECT_EQ(sys.getRootScreen(), GameScreen::Playing);
}

TEST(MenuPrefabs, PauseMenuQuitToMainFiresQuitToMain)
{
    UISystem sys;
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildPauseMenu(canvas, theme, nullptr, sys);

    sys.setRootScreen(GameScreen::Paused);
    auto* quitBtn = findButtonByLabel(canvas, "Quit to Main Menu");
    ASSERT_NE(quitBtn, nullptr);

    quitBtn->onClick.emit();
    EXPECT_EQ(sys.getRootScreen(), GameScreen::MainMenu);
}

TEST(MenuPrefabs, SettingsCloseButtonFiresCloseSettings)
{
    UISystem sys;
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildSettingsMenu(canvas, theme, nullptr, sys);

    // Stage a scenario: Paused → OpenSettings modal. Then fire the close
    // button on a freshly-built settings canvas and verify the modal pops.
    sys.setRootScreen(GameScreen::Paused);
    sys.applyIntent(GameScreenIntent::OpenSettings);
    ASSERT_EQ(sys.getCurrentScreen(), GameScreen::Settings);

    auto* closeBtn = findButtonByLabel(canvas, "ESC  CLOSE");
    ASSERT_NE(closeBtn, nullptr);

    closeBtn->onClick.emit();
    EXPECT_EQ(sys.getRootScreen(), GameScreen::Paused);
    EXPECT_EQ(sys.getTopModalScreen(), GameScreen::None);
}

TEST(MenuPrefabs, LegacyOverloadConnectsNoSignals)
{
    // The 3-arg buildMainMenu must not connect any `onClick` slots —
    // preserves Phase 9C behaviour for games / tests that wire their own
    // signals. `UIButton::interactive` defaults to true in the widget
    // constructor (drives hit-test + hover visuals), so the guard is on
    // the signal-slot count rather than the interactive flag.
    UICanvas canvas;
    UITheme  theme = UITheme::defaultTheme();
    buildMainMenu(canvas, theme, nullptr);

    for (size_t i = 0; i < canvas.getElementCount(); ++i)
    {
        auto* el = canvas.getElementAt(i);
        if (auto* btn = dynamic_cast<UIButton*>(el))
        {
            EXPECT_EQ(btn->onClick.getSlotCount(), 0u)
                << "Legacy 3-arg overload should not wire " << btn->label;
        }
    }
}
