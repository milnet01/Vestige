// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_editor.cpp
/// @brief Phase 10 slice 13.5 — Settings editor orchestrator.
#include "core/settings_editor.h"

#include "input/input_bindings.h"

namespace Vestige
{

SettingsEditor::SettingsEditor(Settings initial, const ApplyTargets& targets)
    : m_applied(initial)
    , m_pending(std::move(initial))
    , m_targets(targets)
{
}

void SettingsEditor::mutate(const std::function<void(Settings&)>& mutator)
{
    mutator(m_pending);
    // Run the same clamp / validation chain as Settings::fromJson
    // so live slider input can't drive subsystems past the policy
    // thresholds (bus gains > 1.0 would clip the mixer, renderScale
    // > 2.0 would blow the GPU budget, strobe Hz > 30 would dodge
    // the persistence cap). Phase 10.9 Slice 1 F8.
    validate(m_pending);
    pushPendingToSinks();
}

SaveStatus SettingsEditor::apply(const std::filesystem::path& settingsPath)
{
    SaveStatus status = m_pending.saveAtomic(settingsPath);
    if (status == SaveStatus::Ok)
    {
        // Advance the applied snapshot only after disk write succeeds.
        // A failed save leaves the subsystems still reflecting pending
        // (from the live-apply invariant) but isDirty() stays true so
        // the caller knows persistence didn't happen.
        m_applied = m_pending;
    }
    return status;
}

void SettingsEditor::revert()
{
    m_pending = m_applied;
    pushPendingToSinks();
}

void SettingsEditor::restoreDisplayDefaults()
{
    m_pending.display = DisplaySettings{};
    pushPendingToSinks();
}

void SettingsEditor::restoreAudioDefaults()
{
    m_pending.audio = AudioSettings{};
    pushPendingToSinks();
}

void SettingsEditor::restoreControlsDefaults()
{
    m_pending.controls = ControlsSettings{};
    // If the caller wired an InputActionMap, also reset its stored
    // bindings to the action-registered defaults so the live state
    // matches the struct default (which has no bindings — an empty
    // wire vector means "whatever was registered on the map").
    if (m_targets.inputMap)
    {
        m_targets.inputMap->resetToDefaults();
    }
    pushPendingToSinks();
}

void SettingsEditor::restoreGameplayDefaults()
{
    m_pending.gameplay = GameplaySettings{};
    pushPendingToSinks();
}

void SettingsEditor::restoreAccessibilityDefaults()
{
    m_pending.accessibility = AccessibilitySettings{};
    pushPendingToSinks();
}

void SettingsEditor::restoreLocalizationDefaults()
{
    m_pending.localization = LocalizationSettings{};
    pushPendingToSinks();
}

void SettingsEditor::restoreAllDefaults()
{
    // Preserve schemaVersion + onboarding across a Restore All — a
    // user who has completed first-run onboarding should NOT have
    // the wizard re-open just because they reset everything else.
    const OnboardingSettings savedOnboarding = m_pending.onboarding;
    const int                savedSchema     = m_pending.schemaVersion;

    m_pending = Settings{};
    m_pending.onboarding    = savedOnboarding;
    m_pending.schemaVersion = savedSchema;
    if (m_targets.inputMap)
    {
        m_targets.inputMap->resetToDefaults();
    }
    pushPendingToSinks();
}

void SettingsEditor::forceLiveApply()
{
    pushPendingToSinks();
}

void SettingsEditor::pushPendingToSinks()
{
    if (m_targets.display)
    {
        applyDisplay(m_pending.display, *m_targets.display);
    }
    if (m_targets.audio)
    {
        applyAudio(m_pending.audio, *m_targets.audio);
    }
    if (m_targets.audioHrtf)
    {
        applyAudioHrtf(m_pending.audio, *m_targets.audioHrtf);
    }
    if (m_targets.audioOutput)
    {
        applyAudioOutput(m_pending.audio, *m_targets.audioOutput);
    }
    if (m_targets.audioAirAbsorb)
    {
        applyAudioAirAbsorption(m_pending.audio, *m_targets.audioAirAbsorb);
    }
    if (m_targets.audioLod)
    {
        applyAudioLod(m_pending.audio, *m_targets.audioLod);
    }
    if (m_targets.audioDeviceHotSwap)
    {
        applyAudioDeviceHotSwap(m_pending.audio, *m_targets.audioDeviceHotSwap);
    }
    if (m_targets.audioLoudness)
    {
        applyAudioLoudness(m_pending.audio, *m_targets.audioLoudness);
    }
    if (m_targets.audioOcclusion)
    {
        applyAudioOcclusion(m_pending.audio, *m_targets.audioOcclusion);
    }
    if (m_targets.audioReverb)
    {
        applyAudioReverb(m_pending.audio, *m_targets.audioReverb);
    }
    if (m_targets.proceduralAudio)
    {
        applyProceduralAudio(m_pending.audio, *m_targets.proceduralAudio);
    }
    if (m_targets.uiAccessibility)
    {
        applyUIAccessibility(m_pending.accessibility,
                             *m_targets.uiAccessibility);
    }
    if (m_targets.rendererAccess)
    {
        applyRendererAccessibility(m_pending.accessibility,
                                   *m_targets.rendererAccess);
    }
    if (m_targets.rendererQuality)
    {
        // Tier-1: maps the quality preset onto the renderer toggles
        // (AA/SSAO/bloom/heavy-post) and reconciles renderScale into the
        // pending DisplaySettings. Idempotent for a consistent preset; a
        // no-op for Custom (the player's hand-tuned knobs stand). The
        // engine reads pending().display.renderScale per-frame.
        applyQualityPreset(m_pending.display.qualityPreset,
                           m_pending.display, *m_targets.rendererQuality);
    }
    if (m_targets.subtitle)
    {
        applySubtitleSettings(m_pending.accessibility,
                              *m_targets.subtitle);
    }
    if (m_targets.photosensitive)
    {
        applyPhotosensitiveSafety(m_pending.accessibility,
                                  *m_targets.photosensitive);
    }
    if (m_targets.inputMap)
    {
        applyInputBindings(m_pending.controls.bindings,
                           *m_targets.inputMap);
    }
    if (m_targets.localization)
    {
        applyLocalization(m_pending.localization, *m_targets.localization);
    }
}

} // namespace Vestige
