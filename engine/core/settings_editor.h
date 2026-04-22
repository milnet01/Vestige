// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_editor.h
/// @brief Phase 10 slice 13.5 — Settings editor orchestrator.
///
/// `SettingsEditor` is the pure-C++ state machine behind the
/// Settings UI. It owns two copies of the `Settings` struct:
///
///   - `m_applied` — the last-committed state. Matches what is
///     persisted to disk and what subsystems believe they have.
///   - `m_pending` — the user's in-progress edits. Diverges from
///     `m_applied` as soon as the user adjusts any control, and
///     re-converges on Apply (commit) or Revert (discard).
///
/// **Live-apply semantics:** every mutation of `m_pending` pushes
/// the changed values through the configured `ApplyTargets` so the
/// user sees / hears the effect immediately. The Apply button only
/// performs the *persistence* step (save to disk + move `m_applied`
/// forward). This gives the UX of "I can see my changes as I drag
/// the slider" while still keeping a clear Apply / Revert contract
/// for discarding unwanted changes (Revert re-applies `m_applied`
/// to the sinks, rolling back the live preview).
///
/// **Per-category restore:** five dedicated reset methods
/// (`restoreDisplayDefaults`, `restoreAudioDefaults`, etc.) plus
/// `restoreAllDefaults`. Granular so a user's 2.0× scale +
/// high-contrast doesn't disappear when they hit a single category
/// reset. Restore operations are live-applied in the same way as
/// regular mutations.
#pragma once

#include "core/settings.h"
#include "core/settings_apply.h"

#include <filesystem>
#include <functional>

namespace Vestige
{

class InputActionMap;   // input/input_bindings.h

class SettingsEditor
{
public:
    /// @brief Subsystem sinks the editor pushes through on every
    ///        `m_pending` mutation (live-apply).
    ///
    /// Any target may be null — pass only the sinks for subsystems
    /// the caller actually wants driven by this editor. Tests
    /// typically leave most null and verify per-category behaviour.
    ///
    /// The editor does NOT take ownership of any of these pointers;
    /// they must outlive the editor.
    struct ApplyTargets
    {
        DisplayApplySink*               display         = nullptr;
        AudioApplySink*                 audio           = nullptr;
        AudioHrtfApplySink*             audioHrtf       = nullptr;
        UIAccessibilityApplySink*       uiAccessibility = nullptr;
        RendererAccessibilityApplySink* rendererAccess  = nullptr;
        SubtitleApplySink*              subtitle        = nullptr;
        PhotosensitiveApplySink*        photosensitive  = nullptr;
        InputActionMap*                 inputMap        = nullptr;
    };

    /// @brief Constructs with a starting `Settings` value + apply
    ///        targets. The starting value populates both
    ///        `m_applied` and `m_pending` — so the editor opens
    ///        in a clean (not-dirty) state.
    ///
    /// The constructor does NOT push through sinks. Subsystems are
    /// assumed to already reflect `initial` (the engine loads
    /// Settings + applies them at bootstrap, then hands the same
    /// Settings to this editor). If the caller wants the sinks
    /// re-pushed on construction, call `forceLiveApply()` after.
    SettingsEditor(Settings initial, ApplyTargets targets);

    const Settings& pending() const { return m_pending; }
    const Settings& applied() const { return m_applied; }

    /// @brief True iff `m_pending` differs from `m_applied`.
    bool isDirty() const { return m_pending != m_applied; }

    /// @brief Apply a caller-supplied mutator to `m_pending`, then
    ///        push the modified state through the apply sinks.
    ///
    /// The mutator is free to change any field across any category.
    /// The editor makes no attempt to diff — it pushes every category
    /// on every mutation. This is intentional; sinks are cheap,
    /// mutations are user-rate (one per slider drag tick), and the
    /// simpler "push everything" policy means a slider that affects
    /// multiple subsystems (e.g. a future "brightness" that touches
    /// both renderer and UI) stays consistent.
    void mutate(const std::function<void(Settings&)>& mutator);

    /// @brief Commit `m_pending` → `m_applied` and persist to disk.
    ///
    /// The live state in subsystems is already `m_pending` (due to
    /// the live-apply invariant), so this method doesn't need to
    /// re-push. It just advances `m_applied` and writes to disk.
    /// A failed save leaves `m_applied` unchanged — the user still
    /// sees the live preview, but `isDirty()` stays true so they
    /// know the change isn't persisted yet.
    SaveStatus apply(const std::filesystem::path& settingsPath);

    /// @brief Discard pending edits: `m_pending` ← `m_applied`, and
    ///        re-push `m_applied` through every sink so subsystems
    ///        roll back from whatever live-preview state they were
    ///        left in.
    void revert();

    /// @brief Reset one category of `m_pending` to struct defaults.
    ///        Live-applies the change through the affected sinks.
    void restoreDisplayDefaults();
    void restoreAudioDefaults();
    void restoreControlsDefaults();
    void restoreGameplayDefaults();
    void restoreAccessibilityDefaults();

    /// @brief Reset every category to struct defaults.
    void restoreAllDefaults();

    /// @brief Re-push `m_pending` through every sink. Useful after
    ///        construction if the caller wants the editor to drive
    ///        subsystems from scratch rather than assuming they
    ///        already reflect the starting Settings.
    void forceLiveApply();

private:
    /// Push `m_pending` through every configured non-null sink.
    void pushPendingToSinks();

    Settings      m_applied;
    Settings      m_pending;
    ApplyTargets  m_targets;
};

} // namespace Vestige
