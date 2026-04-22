// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file game_screen.h
/// @brief Phase 10 in-game UI — pure-function state machine describing
///        which top-level screen a shipped game is currently showing
///        (MainMenu, Playing, Paused, Settings, Loading, …).
///
/// Headless model: every transition is a pure function of
/// `(current screen, intent)` returning the next screen. No engine
/// state, no GL context, no allocations — this file and
/// `game_screen.cpp` can be unit-tested in isolation and reused by
/// any harness (editor preview panel, game build, headless bot).
///
/// The binary `EditorMode::{EDIT, PLAY}` owned by the editor is too
/// coarse for a shipped game that needs a main menu, pause menu,
/// settings overlay, and loading handoff. `GameScreen` is that
/// missing layer. The editor keeps its own mode; headless game
/// builds opt into `GameScreen` via `UISystem::setRootScreen`.
///
/// Transition table (informal):
/// @code
///   None       --OpenMainMenu--> MainMenu
///   MainMenu   --NewWalkthrough-> Loading     (deferred: engine arms assets)
///   MainMenu   --Continue------> Loading     (deferred: engine arms savegame)
///   MainMenu   --OpenSettings--> Settings
///   MainMenu   --QuitToDesktop-> Exiting
///   Loading    --LoadingComplete> Playing
///   Playing    --Pause---------> Paused
///   Playing    --QuitToMain----> MainMenu
///   Paused     --Resume--------> Playing
///   Paused     --OpenSettings--> Settings
///   Paused     --QuitToMain----> MainMenu
///   Paused     --QuitToDesktop-> Exiting
///   Settings   --CloseSettings-> (previous screen — MainMenu or Paused)
/// @endcode
///
/// Any intent that isn't valid for the current screen returns the
/// current screen unchanged (no-op). This means callers can fire
/// intents defensively without branching on the current screen
/// themselves.
///
/// @note `Settings` transitions need to remember where they came
///       from (MainMenu vs Paused). The pure function cannot carry
///       that state on its own — `UISystem` stacks the previous root
///       screen as a modal-pop target. `applyGameScreenIntent` treats
///       `CloseSettings` as "drop back to MainMenu" when called
///       against `Settings` alone; the stack behaviour lives in
///       slice 12.2.
#pragma once

namespace Vestige
{

/// @brief Top-level UI screens a shipped game can present.
///
/// `None` means the UISystem has no root screen — used by the
/// editor (viewport owns the frame) and headless harnesses that
/// drive widgets directly.
enum class GameScreen
{
    None,       ///< No root screen; editor / headless default.
    MainMenu,   ///< Cold-start menu — New, Continue, Settings, Quit.
    Loading,    ///< Transient screen while assets arm for Playing.
    Playing,    ///< World simulation active, HUD visible.
    Paused,     ///< World simulation suspended, pause menu visible.
    Settings,   ///< Settings overlay — openable from MainMenu or Paused.
    Exiting,    ///< Terminal sink — Engine shuts down on entry.
};

/// @brief Intents a caller (button, engine event, bind) can fire at
///        the state machine.
///
/// Intents that don't apply to the current screen are silently
/// dropped — no-op rather than crash. This keeps button wiring
/// robust against accidental multi-fire or stale signals.
enum class GameScreenIntent
{
    OpenMainMenu,     ///< None → MainMenu. Cold-start entry point.
    NewWalkthrough,   ///< MainMenu → Loading. New scene, no savegame.
    Continue,         ///< MainMenu → Loading. Resume from savegame.
    OpenSettings,     ///< MainMenu/Paused → Settings.
    CloseSettings,    ///< Settings → (back to MainMenu or Paused).
    Pause,            ///< Playing → Paused.
    Resume,           ///< Paused → Playing.
    QuitToMain,       ///< Playing/Paused → MainMenu.
    QuitToDesktop,    ///< MainMenu/Paused → Exiting.
    LoadingComplete,  ///< Loading → Playing (fired by asset arming).
};

/// @brief Applies an intent to the current screen via the pure
///        transition table.
///
/// @param current The screen the game is currently showing.
/// @param intent  The intent to apply.
/// @return        The next screen. Equal to `current` when the
///                intent is not valid for that screen (no-op).
///
/// @note This function is total and referentially transparent —
///       no statics, no side effects, safe to call from any thread
///       and from test harnesses.
GameScreen applyGameScreenIntent(GameScreen current, GameScreenIntent intent);

/// @brief Human-readable label for a `GameScreen` (logging / UI /
///        editor panel). Never returns nullptr.
const char* gameScreenLabel(GameScreen screen);

/// @brief Human-readable label for a `GameScreenIntent`. Never
///        returns nullptr.
const char* gameScreenIntentLabel(GameScreenIntent intent);

/// @brief True when the screen implies the world simulation
///        (physics, animation, AI, particles) must be suspended.
///
/// `Paused`, `Loading`, `MainMenu`, `Settings`, `Exiting`, and
/// `None` all suspend the world. Only `Playing` runs the sim.
/// UI transitions (tweens, hover highlights) still tick — callers
/// multiply world-system dt by 0 on suspension, UI dt stays real.
bool isWorldSimulationSuspended(GameScreen screen);

/// @brief True when the screen must swallow world-input (movement
///        keys, look axis) so it doesn't leak into the game while
///        a menu is focused.
///
/// Every non-`Playing` screen suppresses world input. This is
/// distinct from `UISystem::wantsCaptureInput()` which tracks the
/// modal / cursor state; this predicate is about the *screen*
/// rather than the widget under the cursor.
bool suppressesWorldInput(GameScreen screen);

} // namespace Vestige
