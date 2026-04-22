// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file first_run_wizard.h
/// @brief Phase 10.5 slice 14.2 — first-run onboarding wizard.
///
/// A two-step modal wizard shown the first time the user launches
/// the editor. Step 1 (Welcome) offers the top-level choice: pick
/// a game-type template, start literally empty, show the built-in
/// demo, or skip for now. Step 2 (Template picker) is a filtered
/// view over the Phase 9D `TemplateDialog` templates with the four
/// featured archetypes surfaced + the remaining four under a
/// "More templates" expander.
///
/// See `docs/PHASE10_5_FIRST_RUN_WIZARD_DESIGN.md` for the full
/// design, the eight sign-off questions, and the 14.x slice plan.
///
/// Design split — pure state machine + ImGui-binding panel:
///
///   - `FirstRunWizardStep` enum + `FirstRunIntent` enum +
///     `FirstRunTransition` struct + `applyFirstRunIntent(...)`
///     pure function model every step change **without referencing
///     ImGui or any Scene**. That makes 14.2's state-transition
///     tests headless and deterministic — 8 of them in
///     `tests/test_first_run_wizard.cpp`.
///
///   - `FirstRunWizard` is the thin ImGui binding that owns the
///     state, renders the modal, forwards user clicks as intents,
///     and hands the caller back a `SceneOp` so the Engine /
///     Editor integration layer (slice 14.4) can apply the
///     chosen scene construction.
///
/// Nothing in this header auto-triggers scene mutation. The caller
/// (slice 14.4) owns the Scene + Renderer + ResourceManager and
/// executes whichever `FirstRunWizardSceneOp` the user selected.
#pragma once

#include "core/settings.h"                         // OnboardingSettings
#include "editor/panels/template_dialog.h"         // GameTemplateConfig

#include <filesystem>
#include <string>
#include <vector>

namespace Vestige
{

// --------------------------------------------------------------
// Pure state machine
// --------------------------------------------------------------

/// @brief Current step in the wizard flow.
enum class FirstRunWizardStep
{
    Welcome,          ///< Top-level choice — pick / empty / demo / skip.
    TemplatePicker,   ///< Step 2 — filtered list over TemplateDialog templates.
    Done,             ///< Terminal — wizard has closed for this session.
};

/// @brief User intents accepted by the wizard. The UI layer fires
///        an intent per button click; the pure-function
///        `applyFirstRunIntent` decides the next step + side effect.
enum class FirstRunIntent
{
    // From Welcome
    PickTemplate,     ///< Advance to the picker.
    StartEmpty,       ///< Apply a literally-empty scene and close.
    ShowDemo,         ///< Apply the engine's built-in demo and close.
    SkipForNow,       ///< Dismiss without picking. Third skip auto-completes.
    // From TemplatePicker
    Back,             ///< Return to Welcome.
    FinishWithTemplate, ///< Apply the selected template and close.
    // Close-path equivalents — window X / Esc.
    CloseAtWelcome,   ///< Same as SkipForNow.
    CloseAtPicker,    ///< Same as Back.
};

/// @brief Scene-construction side effect the UI layer should apply
///        after receiving the transition. Carries no data beyond
///        the tag — "which template" for ApplyTemplate is held by
///        the panel's selection state and passed separately.
enum class FirstRunWizardSceneOp
{
    None,
    ApplyEmpty,       ///< Camera + directional light + ground plane.
    ApplyDemo,        ///< `Engine::setupDemoScene()` showroom.
    ApplyTemplate,    ///< Whatever template the panel had selected when
                      ///  the user clicked Create. Panel state carries index.
};

/// @brief Transition emitted by `applyFirstRunIntent`. Fully describes
///        what the UI should render next, what side effect to apply,
///        and whether to close the wizard.
struct FirstRunTransition
{
    FirstRunWizardStep    step       = FirstRunWizardStep::Welcome;
    /// Post-intent onboarding state. Caller writes this back to
    /// `Settings.onboarding` so the v2 migration + atomic save
    /// pipeline persists it on the next Apply.
    OnboardingSettings    onboarding;
    FirstRunWizardSceneOp sceneOp    = FirstRunWizardSceneOp::None;
    bool                  closed     = false;
};

/// @brief Pure-function step. Deterministic, headless-testable.
///
/// @param currentStep    Where the user is now.
/// @param current        Current onboarding flags (read only).
/// @param intent         What the user just did.
/// @param nowIso         Wall-clock ISO-8601 UTC string to stamp
///                        `completedAt` when the intent completes the
///                        wizard. Caller passes the real clock in
///                        production; tests pass a fixed string.
///
/// @returns the next step, the next onboarding values, the
///          scene-construction op the UI should apply, and whether
///          the wizard should close.
///
/// Skip semantics (Q7 resolution): the first SkipForNow increments
/// `skipCount` without flipping `hasCompletedFirstRun`; the second
/// SkipForNow (skipCount reaching 2) flips completion + stamps
/// `completedAt`. Close-at-welcome maps to SkipForNow; close-at-picker
/// maps to Back without incrementing skipCount.
FirstRunTransition applyFirstRunIntent(
    FirstRunWizardStep currentStep,
    const OnboardingSettings& current,
    FirstRunIntent intent,
    const std::string& nowIso);

// --------------------------------------------------------------
// Featured-template filter (Q1 resolution)
// --------------------------------------------------------------

/// @brief The four featured templates surfaced on Step 2:
///        3D First Person, 3D Third Person, 2.5D Side-Scroller,
///        Isometric. Other templates live under a "More templates"
///        expander — see `moreTemplates`.
std::vector<GameTemplateConfig> featuredTemplates();

/// @brief The four non-featured templates that live behind the
///        "More templates" expander: Top-Down, Point-and-Click,
///        2D Side-Scroller, 2D Shmup.
std::vector<GameTemplateConfig> moreTemplates();

/// @brief `featuredTemplates()` followed by `moreTemplates()`.
///        Order matches the visual order in the picker.
std::vector<GameTemplateConfig> allWizardTemplates();

/// @brief Filter a template list down to entries whose `requiredAssets`
///        paths all exist under `assetRoot`. Templates with empty
///        `requiredAssets` are always kept.
///
/// Phase 10.5 slice 14.3 (Q4 resolution): the biblical walkthrough
/// template — when it lands in the private sibling repo — carries
/// its tabernacle textures + HDRI as `requiredAssets`. Public-engine
/// clones don't have those files, so the wizard's picker filters
/// the option out automatically. When the maintainer's private
/// assets are on disk, the option reappears with no code change.
///
/// The `File → New from Template…` menu path does NOT call this —
/// that surface always lists every template so power users can
/// discover what exists (per §6 of the design doc).
std::vector<GameTemplateConfig> filterByAvailability(
    const std::vector<GameTemplateConfig>& templates,
    const std::filesystem::path& assetRoot);

// --------------------------------------------------------------
// ImGui-binding panel
// --------------------------------------------------------------

class FirstRunWizard
{
public:
    /// @brief Attach to the OnboardingSettings sub-struct on the
    ///        engine's live `Settings`. Caller keeps ownership; the
    ///        wizard reads + writes in place so the next
    ///        `Settings::saveAtomic` pushes the new state to disk.
    ///
    /// Auto-opens when `!onboarding->hasCompletedFirstRun`. Does
    /// not re-open within a session after a terminal transition.
    ///
    /// @param assetRoot Root directory for resolving template
    ///        `requiredAssets` paths. Slice 14.4 passes the engine's
    ///        configured `assetPath`. Pass an empty path to disable
    ///        filtering (useful for tests that don't care about
    ///        availability).
    void initialize(OnboardingSettings* onboarding,
                    std::filesystem::path assetRoot = {});

    /// @brief Renders the modal and returns any scene op the user
    ///        selected this frame.
    ///
    /// Slice 14.4 uses the op to call either `applyEmptyScene`,
    /// `Engine::setupDemoScene`, or `TemplateDialog::applyTemplate`
    /// with the panel's current `selectedTemplateIndex()`.
    FirstRunWizardSceneOp draw();

    /// @brief Re-open from the Help menu. Bypasses the auto-open
    ///        guard. Resets the wizard to Step 1.
    void openFromHelpMenu();

    bool                isOpen()                const { return m_open; }
    FirstRunWizardStep  currentStep()           const { return m_step; }
    int                 selectedTemplateIndex() const { return m_selectedIndex; }

private:
    /// Runs an intent through the pure function, writes the result
    /// back to m_step + the attached onboarding struct, and returns
    /// the op for the UI layer to apply.
    FirstRunWizardSceneOp fire(FirstRunIntent intent);

    OnboardingSettings*   m_onboarding    = nullptr;
    FirstRunWizardStep    m_step          = FirstRunWizardStep::Welcome;
    bool                  m_open          = false;
    int                   m_selectedIndex = 0;   ///< Index into the filtered combined list.
    bool                  m_showMore      = false;///< "More templates" expander state.
    std::filesystem::path m_assetRoot;            ///< For requiredAssets filtering (empty = disabled).
};

} // namespace Vestige
