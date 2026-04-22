// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_game_screen.cpp
/// @brief Slice 12.1 coverage for the `GameScreen` pure-function
///        state machine. Walks every (screen, intent) pair against
///        the expected transition table so regressions in the
///        switch ladder surface immediately.

#include <gtest/gtest.h>

#include <string>

#include "ui/game_screen.h"

using namespace Vestige;

namespace
{
constexpr GameScreen kAllScreens[] = {
    GameScreen::None,
    GameScreen::MainMenu,
    GameScreen::Loading,
    GameScreen::Playing,
    GameScreen::Paused,
    GameScreen::Settings,
    GameScreen::Exiting,
};

constexpr GameScreenIntent kAllIntents[] = {
    GameScreenIntent::OpenMainMenu,
    GameScreenIntent::NewWalkthrough,
    GameScreenIntent::Continue,
    GameScreenIntent::OpenSettings,
    GameScreenIntent::CloseSettings,
    GameScreenIntent::Pause,
    GameScreenIntent::Resume,
    GameScreenIntent::QuitToMain,
    GameScreenIntent::QuitToDesktop,
    GameScreenIntent::LoadingComplete,
};
} // namespace

// -- None -------------------------------------------------------------------

TEST(GameScreenTransitions, NoneAcceptsOnlyOpenMainMenu)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::None, GameScreenIntent::OpenMainMenu),
              GameScreen::MainMenu);
}

TEST(GameScreenTransitions, NoneRejectsEveryOtherIntent)
{
    for (auto intent : kAllIntents)
    {
        if (intent == GameScreenIntent::OpenMainMenu) continue;
        EXPECT_EQ(applyGameScreenIntent(GameScreen::None, intent), GameScreen::None)
            << "intent=" << gameScreenIntentLabel(intent);
    }
}

// -- MainMenu ---------------------------------------------------------------

TEST(GameScreenTransitions, MainMenuNewWalkthroughGoesToLoading)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::MainMenu, GameScreenIntent::NewWalkthrough),
              GameScreen::Loading);
}

TEST(GameScreenTransitions, MainMenuContinueGoesToLoading)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::MainMenu, GameScreenIntent::Continue),
              GameScreen::Loading);
}

TEST(GameScreenTransitions, MainMenuOpenSettingsGoesToSettings)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::MainMenu, GameScreenIntent::OpenSettings),
              GameScreen::Settings);
}

TEST(GameScreenTransitions, MainMenuQuitToDesktopGoesToExiting)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::MainMenu, GameScreenIntent::QuitToDesktop),
              GameScreen::Exiting);
}

TEST(GameScreenTransitions, MainMenuRejectsInGameIntents)
{
    for (auto intent : {GameScreenIntent::Pause,
                        GameScreenIntent::Resume,
                        GameScreenIntent::QuitToMain,
                        GameScreenIntent::LoadingComplete,
                        GameScreenIntent::CloseSettings,
                        GameScreenIntent::OpenMainMenu})
    {
        EXPECT_EQ(applyGameScreenIntent(GameScreen::MainMenu, intent), GameScreen::MainMenu)
            << "intent=" << gameScreenIntentLabel(intent);
    }
}

// -- Loading ----------------------------------------------------------------

TEST(GameScreenTransitions, LoadingCompleteGoesToPlaying)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::Loading, GameScreenIntent::LoadingComplete),
              GameScreen::Playing);
}

TEST(GameScreenTransitions, LoadingRejectsEveryOtherIntent)
{
    for (auto intent : kAllIntents)
    {
        if (intent == GameScreenIntent::LoadingComplete) continue;
        EXPECT_EQ(applyGameScreenIntent(GameScreen::Loading, intent), GameScreen::Loading)
            << "intent=" << gameScreenIntentLabel(intent);
    }
}

// -- Playing ----------------------------------------------------------------

TEST(GameScreenTransitions, PlayingPauseGoesToPaused)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::Playing, GameScreenIntent::Pause),
              GameScreen::Paused);
}

TEST(GameScreenTransitions, PlayingQuitToMainGoesToMainMenu)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::Playing, GameScreenIntent::QuitToMain),
              GameScreen::MainMenu);
}

TEST(GameScreenTransitions, PlayingRejectsInvalidIntents)
{
    for (auto intent : {GameScreenIntent::Resume,
                        GameScreenIntent::OpenSettings,
                        GameScreenIntent::CloseSettings,
                        GameScreenIntent::NewWalkthrough,
                        GameScreenIntent::Continue,
                        GameScreenIntent::LoadingComplete,
                        GameScreenIntent::OpenMainMenu,
                        GameScreenIntent::QuitToDesktop})
    {
        EXPECT_EQ(applyGameScreenIntent(GameScreen::Playing, intent), GameScreen::Playing)
            << "intent=" << gameScreenIntentLabel(intent);
    }
}

// -- Paused -----------------------------------------------------------------

TEST(GameScreenTransitions, PausedResumeGoesToPlaying)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::Paused, GameScreenIntent::Resume),
              GameScreen::Playing);
}

TEST(GameScreenTransitions, PausedOpenSettingsGoesToSettings)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::Paused, GameScreenIntent::OpenSettings),
              GameScreen::Settings);
}

TEST(GameScreenTransitions, PausedQuitToMainGoesToMainMenu)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::Paused, GameScreenIntent::QuitToMain),
              GameScreen::MainMenu);
}

TEST(GameScreenTransitions, PausedQuitToDesktopGoesToExiting)
{
    EXPECT_EQ(applyGameScreenIntent(GameScreen::Paused, GameScreenIntent::QuitToDesktop),
              GameScreen::Exiting);
}

TEST(GameScreenTransitions, PausedRejectsInvalidIntents)
{
    for (auto intent : {GameScreenIntent::Pause,
                        GameScreenIntent::NewWalkthrough,
                        GameScreenIntent::Continue,
                        GameScreenIntent::LoadingComplete,
                        GameScreenIntent::OpenMainMenu,
                        GameScreenIntent::CloseSettings})
    {
        EXPECT_EQ(applyGameScreenIntent(GameScreen::Paused, intent), GameScreen::Paused)
            << "intent=" << gameScreenIntentLabel(intent);
    }
}

// -- Settings ---------------------------------------------------------------

TEST(GameScreenTransitions, SettingsCloseGoesToMainMenuWithoutStack)
{
    // Pure function falls back to MainMenu when there is no stack;
    // UISystem (slice 12.2) overrides via popModalScreen to return
    // to whichever screen pushed Settings.
    EXPECT_EQ(applyGameScreenIntent(GameScreen::Settings, GameScreenIntent::CloseSettings),
              GameScreen::MainMenu);
}

TEST(GameScreenTransitions, SettingsRejectsEveryOtherIntent)
{
    for (auto intent : kAllIntents)
    {
        if (intent == GameScreenIntent::CloseSettings) continue;
        EXPECT_EQ(applyGameScreenIntent(GameScreen::Settings, intent), GameScreen::Settings)
            << "intent=" << gameScreenIntentLabel(intent);
    }
}

// -- Exiting ----------------------------------------------------------------

TEST(GameScreenTransitions, ExitingIsTerminal)
{
    for (auto intent : kAllIntents)
    {
        EXPECT_EQ(applyGameScreenIntent(GameScreen::Exiting, intent), GameScreen::Exiting)
            << "intent=" << gameScreenIntentLabel(intent);
    }
}

// -- Purity / determinism ---------------------------------------------------

TEST(GameScreenTransitions, RepeatedApplicationIsIdempotentForNoOps)
{
    // A rejected intent applied N times must still equal the original screen.
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(applyGameScreenIntent(GameScreen::Playing, GameScreenIntent::Resume),
                  GameScreen::Playing);
    }
}

TEST(GameScreenTransitions, PauseResumeIsReversible)
{
    GameScreen s = GameScreen::Playing;
    s = applyGameScreenIntent(s, GameScreenIntent::Pause);
    EXPECT_EQ(s, GameScreen::Paused);
    s = applyGameScreenIntent(s, GameScreenIntent::Resume);
    EXPECT_EQ(s, GameScreen::Playing);
}

TEST(GameScreenTransitions, ColdStartChainReachesPlaying)
{
    GameScreen s = GameScreen::None;
    s = applyGameScreenIntent(s, GameScreenIntent::OpenMainMenu);
    EXPECT_EQ(s, GameScreen::MainMenu);
    s = applyGameScreenIntent(s, GameScreenIntent::NewWalkthrough);
    EXPECT_EQ(s, GameScreen::Loading);
    s = applyGameScreenIntent(s, GameScreenIntent::LoadingComplete);
    EXPECT_EQ(s, GameScreen::Playing);
}

// -- Predicates -------------------------------------------------------------

TEST(GameScreenPredicates, OnlyPlayingRunsWorldSimulation)
{
    for (auto screen : kAllScreens)
    {
        bool suspended = isWorldSimulationSuspended(screen);
        if (screen == GameScreen::Playing)
        {
            EXPECT_FALSE(suspended) << "screen=" << gameScreenLabel(screen);
        }
        else
        {
            EXPECT_TRUE(suspended) << "screen=" << gameScreenLabel(screen);
        }
    }
}

TEST(GameScreenPredicates, OnlyPlayingAcceptsWorldInput)
{
    for (auto screen : kAllScreens)
    {
        bool suppressed = suppressesWorldInput(screen);
        if (screen == GameScreen::Playing)
        {
            EXPECT_FALSE(suppressed) << "screen=" << gameScreenLabel(screen);
        }
        else
        {
            EXPECT_TRUE(suppressed) << "screen=" << gameScreenLabel(screen);
        }
    }
}

// -- Labels -----------------------------------------------------------------

TEST(GameScreenLabels, EveryScreenHasUniqueLabel)
{
    const char* labels[] = {
        gameScreenLabel(GameScreen::None),
        gameScreenLabel(GameScreen::MainMenu),
        gameScreenLabel(GameScreen::Loading),
        gameScreenLabel(GameScreen::Playing),
        gameScreenLabel(GameScreen::Paused),
        gameScreenLabel(GameScreen::Settings),
        gameScreenLabel(GameScreen::Exiting),
    };
    for (const char* label : labels)
    {
        ASSERT_NE(label, nullptr);
        EXPECT_GT(std::string(label).length(), 0u);
    }
    // Distinct labels — sanity check that the switch doesn't alias two.
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
    {
        for (size_t j = i + 1; j < sizeof(labels) / sizeof(labels[0]); ++j)
        {
            EXPECT_STRNE(labels[i], labels[j]);
        }
    }
}

TEST(GameScreenLabels, EveryIntentHasNonEmptyLabel)
{
    for (auto intent : kAllIntents)
    {
        const char* label = gameScreenIntentLabel(intent);
        ASSERT_NE(label, nullptr);
        EXPECT_GT(std::string(label).length(), 0u);
    }
}
