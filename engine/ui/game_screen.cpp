// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file game_screen.cpp
/// @brief Pure-function transition table for `GameScreen`. See
///        `game_screen.h` for the informal diagram and slice context.

#include "ui/game_screen.h"

namespace Vestige
{

GameScreen applyGameScreenIntent(GameScreen current, GameScreenIntent intent)
{
    switch (current)
    {
        case GameScreen::None:
            if (intent == GameScreenIntent::OpenMainMenu)
            {
                return GameScreen::MainMenu;
            }
            return current;

        case GameScreen::MainMenu:
            switch (intent)
            {
                case GameScreenIntent::NewWalkthrough: return GameScreen::Loading;
                case GameScreenIntent::Continue:       return GameScreen::Loading;
                case GameScreenIntent::OpenSettings:   return GameScreen::Settings;
                case GameScreenIntent::QuitToDesktop:  return GameScreen::Exiting;
                default:                               return current;
            }

        case GameScreen::Loading:
            if (intent == GameScreenIntent::LoadingComplete)
            {
                return GameScreen::Playing;
            }
            return current;

        case GameScreen::Playing:
            switch (intent)
            {
                case GameScreenIntent::Pause:      return GameScreen::Paused;
                case GameScreenIntent::QuitToMain: return GameScreen::MainMenu;
                default:                           return current;
            }

        case GameScreen::Paused:
            switch (intent)
            {
                case GameScreenIntent::Resume:        return GameScreen::Playing;
                case GameScreenIntent::OpenSettings:  return GameScreen::Settings;
                case GameScreenIntent::QuitToMain:    return GameScreen::MainMenu;
                case GameScreenIntent::QuitToDesktop: return GameScreen::Exiting;
                default:                              return current;
            }

        case GameScreen::Settings:
            // Settings without a stack context drops back to MainMenu.
            // UISystem (slice 12.2) owns the stack that preserves the
            // true previous screen (MainMenu vs Paused) and routes
            // CloseSettings through `popModalScreen` instead.
            if (intent == GameScreenIntent::CloseSettings)
            {
                return GameScreen::MainMenu;
            }
            return current;

        case GameScreen::Exiting:
            // Terminal — Engine should be tearing down; ignore intents.
            return current;
    }

    return current;
}

const char* gameScreenLabel(GameScreen screen)
{
    switch (screen)
    {
        case GameScreen::None:     return "None";
        case GameScreen::MainMenu: return "MainMenu";
        case GameScreen::Loading:  return "Loading";
        case GameScreen::Playing:  return "Playing";
        case GameScreen::Paused:   return "Paused";
        case GameScreen::Settings: return "Settings";
        case GameScreen::Exiting:  return "Exiting";
    }
    return "Unknown";
}

const char* gameScreenIntentLabel(GameScreenIntent intent)
{
    switch (intent)
    {
        case GameScreenIntent::OpenMainMenu:    return "OpenMainMenu";
        case GameScreenIntent::NewWalkthrough:  return "NewWalkthrough";
        case GameScreenIntent::Continue:        return "Continue";
        case GameScreenIntent::OpenSettings:    return "OpenSettings";
        case GameScreenIntent::CloseSettings:   return "CloseSettings";
        case GameScreenIntent::Pause:           return "Pause";
        case GameScreenIntent::Resume:          return "Resume";
        case GameScreenIntent::QuitToMain:      return "QuitToMain";
        case GameScreenIntent::QuitToDesktop:   return "QuitToDesktop";
        case GameScreenIntent::LoadingComplete: return "LoadingComplete";
    }
    return "Unknown";
}

bool isWorldSimulationSuspended(GameScreen screen)
{
    return screen != GameScreen::Playing;
}

bool suppressesWorldInput(GameScreen screen)
{
    return screen != GameScreen::Playing;
}

} // namespace Vestige
