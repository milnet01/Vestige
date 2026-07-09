// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_panel.cpp
/// @brief Non-GL tests for the editor AudioPanel — open/close
///        toggle, zone add/select/remove, mute/solo sets, and the
///        effective-gain router for the AudioSystem integration
///        point.

#include <gtest/gtest.h>

#include "audio/acoustic_probe_component.h"
#include "audio/reverb_zone_component.h"
#include "core/settings.h"
#include "core/settings_editor.h"
#include "editor/panels/audio_panel.h"
#include "scene/entity.h"
#include "scene/scene.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-4f;
}

// -- Defaults + open/close ----------------------------------------

TEST(AudioPanel, DefaultsAreClosed)
{
    AudioPanel p;
    EXPECT_FALSE(p.isOpen());
    EXPECT_FALSE(p.isZoneOverlayEnabled());
    EXPECT_EQ(p.selectedReverbZone(),  0u);   // 0 = no entity selected.
    EXPECT_EQ(p.selectedAmbientZone(), -1);
    EXPECT_TRUE(p.ambientZones().empty());
    EXPECT_FALSE(p.hasAnySoloedSource());
}

TEST(AudioPanel, SetOpenToggles)
{
    AudioPanel p;
    p.setOpen(true);
    EXPECT_TRUE(p.isOpen());
    p.toggleOpen();
    EXPECT_FALSE(p.isOpen());
    p.toggleOpen();
    EXPECT_TRUE(p.isOpen());
}

// -- Mixer + ducking defaults -------------------------------------

TEST(AudioPanel, MixerDefaultsToUnityGains)
{
    AudioPanel p;
    for (auto bus : {AudioBus::Master, AudioBus::Music, AudioBus::Voice,
                      AudioBus::Sfx, AudioBus::Ambient, AudioBus::Ui})
    {
        EXPECT_NEAR(effectiveBusGain(p.mixer(), bus), 1.0f, kEps);
    }
}

TEST(AudioPanel, DuckingStartsUntriggeredAtUnity)
{
    AudioPanel p;
    EXPECT_FALSE(p.duckingState().triggered);
    EXPECT_NEAR(p.duckingState().currentGain, 1.0f, kEps);
}

// -- Reverb zone management (scene-backed ReverbZoneComponent) -----

namespace
{
int countReverbZones(Scene& scene)
{
    int n = 0;
    scene.forEachEntity([&](Entity& e)
    {
        if (e.getComponent<ReverbZoneComponent>() != nullptr) ++n;
    });
    return n;
}
}

TEST(AudioPanel, CreateReverbZoneAddsComponentEntityAndSelectsIt)
{
    AudioPanel p;
    Scene scene("test");

    Entity* e = p.createReverbZone(scene);
    ASSERT_NE(e, nullptr);
    EXPECT_NE(e->getComponent<ReverbZoneComponent>(), nullptr);
    EXPECT_EQ(p.selectedReverbZone(), e->getId());
    EXPECT_EQ(countReverbZones(scene), 1);

    // A second create is an independent entity and becomes the selection.
    Entity* e2 = p.createReverbZone(scene);
    ASSERT_NE(e2, nullptr);
    EXPECT_NE(e2->getId(), e->getId());
    EXPECT_EQ(p.selectedReverbZone(), e2->getId());
    EXPECT_EQ(countReverbZones(scene), 2);
}

TEST(AudioPanel, RemoveSelectedReverbZoneClearsSelection)
{
    AudioPanel p;
    Scene scene("test");

    Entity* e = p.createReverbZone(scene);
    const std::uint32_t id = e->getId();
    EXPECT_TRUE(p.removeReverbZone(scene, id));
    EXPECT_EQ(p.selectedReverbZone(), 0u);
    EXPECT_EQ(countReverbZones(scene), 0);
}

TEST(AudioPanel, RemoveNonSelectedReverbZoneKeepsSelection)
{
    AudioPanel p;
    Scene scene("test");

    Entity* first  = p.createReverbZone(scene);
    Entity* second = p.createReverbZone(scene);  // now selected
    const std::uint32_t firstId  = first->getId();
    const std::uint32_t secondId = second->getId();

    EXPECT_TRUE(p.removeReverbZone(scene, firstId));
    EXPECT_EQ(p.selectedReverbZone(), secondId);  // untouched
    EXPECT_EQ(countReverbZones(scene), 1);
}

TEST(AudioPanel, RemoveUnknownReverbZoneIsNoOp)
{
    AudioPanel p;
    Scene scene("test");

    p.createReverbZone(scene);
    EXPECT_FALSE(p.removeReverbZone(scene, 999999u));
    EXPECT_EQ(countReverbZones(scene), 1);
}

// -- Acoustic probe management (scene-backed AcousticProbeComponent, B5) --

namespace
{
int countAcousticProbes(Scene& scene)
{
    int n = 0;
    scene.forEachEntity([&](Entity& e)
    {
        if (e.getComponent<AcousticProbeComponent>() != nullptr) ++n;
    });
    return n;
}
}

TEST(AudioPanel, CreateAcousticProbeAddsComponentEntityAndSelectsIt)
{
    AudioPanel p;
    Scene scene("test");

    Entity* e = p.createAcousticProbe(scene);
    ASSERT_NE(e, nullptr);
    EXPECT_NE(e->getComponent<AcousticProbeComponent>(), nullptr);
    EXPECT_EQ(p.selectedAcousticProbe(), e->getId());
    EXPECT_EQ(countAcousticProbes(scene), 1);

    Entity* e2 = p.createAcousticProbe(scene);
    ASSERT_NE(e2, nullptr);
    EXPECT_NE(e2->getId(), e->getId());
    EXPECT_EQ(p.selectedAcousticProbe(), e2->getId());  // newest is selected
    EXPECT_EQ(countAcousticProbes(scene), 2);
}

TEST(AudioPanel, RemoveSelectedAcousticProbeClearsSelection)
{
    AudioPanel p;
    Scene scene("test");

    Entity* e = p.createAcousticProbe(scene);
    const std::uint32_t id = e->getId();
    EXPECT_TRUE(p.removeAcousticProbe(scene, id));
    EXPECT_EQ(p.selectedAcousticProbe(), 0u);
    EXPECT_EQ(countAcousticProbes(scene), 0);
}

TEST(AudioPanel, RemoveNonSelectedAcousticProbeKeepsSelection)
{
    AudioPanel p;
    Scene scene("test");

    Entity* first  = p.createAcousticProbe(scene);
    Entity* second = p.createAcousticProbe(scene);  // now selected
    const std::uint32_t firstId  = first->getId();
    const std::uint32_t secondId = second->getId();

    EXPECT_TRUE(p.removeAcousticProbe(scene, firstId));
    EXPECT_EQ(p.selectedAcousticProbe(), secondId);  // untouched
    EXPECT_EQ(countAcousticProbes(scene), 1);
}

TEST(AudioPanel, ReverbZoneAndAcousticProbeSelectionsAreIndependent)
{
    AudioPanel p;
    Scene scene("test");

    Entity* zone  = p.createReverbZone(scene);
    Entity* probe = p.createAcousticProbe(scene);
    EXPECT_EQ(p.selectedReverbZone(),   zone->getId());
    EXPECT_EQ(p.selectedAcousticProbe(), probe->getId());

    // Removing the probe leaves the zone selection intact and vice versa.
    EXPECT_TRUE(p.removeAcousticProbe(scene, probe->getId()));
    EXPECT_EQ(p.selectedReverbZone(), zone->getId());
    EXPECT_EQ(p.selectedAcousticProbe(), 0u);
}

// -- Ambient zone management --------------------------------------

TEST(AudioPanel, AddAmbientZoneReturnsIndex)
{
    AudioPanel p;
    AudioPanel::AmbientZoneInstance z;
    z.name = "Wind";
    EXPECT_EQ(p.addAmbientZone(z), 0);
    EXPECT_EQ(p.ambientZones().size(), 1u);
}

TEST(AudioPanel, RemoveAmbientZoneMirrorsReverbBehavior)
{
    AudioPanel p;
    p.addAmbientZone({});
    p.addAmbientZone({});
    p.selectAmbientZone(1);

    EXPECT_TRUE(p.removeAmbientZone(1));
    EXPECT_EQ(p.selectedAmbientZone(), -1);
    EXPECT_EQ(p.ambientZones().size(), 1u);
}

// -- Mute / solo --------------------------------------------------

TEST(AudioPanel, MuteTogglesSourceState)
{
    AudioPanel p;
    EXPECT_FALSE(p.isSourceMuted(42));
    p.setSourceMuted(42, true);
    EXPECT_TRUE(p.isSourceMuted(42));
    p.setSourceMuted(42, false);
    EXPECT_FALSE(p.isSourceMuted(42));
}

TEST(AudioPanel, SoloTogglesSourceState)
{
    AudioPanel p;
    EXPECT_FALSE(p.hasAnySoloedSource());
    p.setSourceSoloed(7, true);
    EXPECT_TRUE(p.isSourceSoloed(7));
    EXPECT_TRUE(p.hasAnySoloedSource());
    p.setSourceSoloed(7, false);
    EXPECT_FALSE(p.hasAnySoloedSource());
}

// -- Effective gain routing ---------------------------------------

TEST(AudioPanel, MutedSourceEffectiveGainIsZero)
{
    AudioPanel p;
    p.setSourceMuted(42, true);
    EXPECT_NEAR(p.computeEffectiveSourceGain(42, AudioBus::Sfx), 0.0f, kEps);
}

TEST(AudioPanel, SoloedSourcesOnlyPlay)
{
    AudioPanel p;
    p.setSourceSoloed(7, true);

    // 7 is soloed → audible.
    EXPECT_NEAR(p.computeEffectiveSourceGain(7, AudioBus::Music), 1.0f, kEps);
    // 8 is not soloed and at least one source is → silent.
    EXPECT_NEAR(p.computeEffectiveSourceGain(8, AudioBus::Music), 0.0f, kEps);
}

TEST(AudioPanel, MutedBeatsSolo)
{
    // If a source is both muted and soloed, mute wins (convention
    // matches every DAW — mute is the harder kill switch).
    AudioPanel p;
    p.setSourceSoloed(7, true);
    p.setSourceMuted(7, true);
    EXPECT_NEAR(p.computeEffectiveSourceGain(7, AudioBus::Music), 0.0f, kEps);
}

TEST(AudioPanel, EffectiveGainAppliesBusAndDucking)
{
    AudioPanel p;
    p.mixer().busGain[static_cast<std::size_t>(AudioBus::Master)] = 0.5f;
    p.mixer().busGain[static_cast<std::size_t>(AudioBus::Music)]  = 0.4f;
    p.duckingState().currentGain = 0.5f;
    // Master*Music*Duck = 0.5 * 0.4 * 0.5 = 0.1
    EXPECT_NEAR(p.computeEffectiveSourceGain(1, AudioBus::Music), 0.1f, kEps);
}

TEST(AudioPanel, EffectiveGainClampedToUnitRange)
{
    AudioPanel p;
    p.mixer().busGain[static_cast<std::size_t>(AudioBus::Master)] = 2.0f;
    p.mixer().busGain[static_cast<std::size_t>(AudioBus::Music)]  = 2.0f;
    p.duckingState().currentGain = 2.0f;
    EXPECT_NEAR(p.computeEffectiveSourceGain(1, AudioBus::Music), 1.0f, kEps);
}

// -- Debug overlay toggle -----------------------------------------

TEST(AudioPanel, ZoneOverlayToggle)
{
    AudioPanel p;
    EXPECT_FALSE(p.isZoneOverlayEnabled());
    p.setZoneOverlayEnabled(true);
    EXPECT_TRUE(p.isZoneOverlayEnabled());
}

// -- Phase 10.7 slice A3: engine-mixer wire-up --------------------

TEST(AudioPanelWire, UnwiredPanelUsesLocalMixer)
{
    AudioPanel p;
    // The default panel has no engine mixer; mixer() returns the
    // internal fallback and edits stay local.
    p.mixer().setBusGain(AudioBus::Music, 0.25f);
    EXPECT_NEAR(p.mixer().getBusGain(AudioBus::Music), 0.25f, kEps);
}

TEST(AudioPanelWire, WiringEngineMixerRedirectsReads)
{
    AudioPanel p;
    AudioMixer engineMixer;
    engineMixer.setBusGain(AudioBus::Music, 0.33f);
    p.wireEngineMixer(&engineMixer, nullptr);
    // Reading through mixer() now shows the engine mixer's value,
    // not whatever the panel's local fallback last held.
    EXPECT_NEAR(p.mixer().getBusGain(AudioBus::Music), 0.33f, kEps);
}

TEST(AudioPanelWire, WiringWithNullEngineMixerFallsBackToLocal)
{
    AudioPanel p;
    p.mixer().setBusGain(AudioBus::Sfx, 0.6f);
    // Null engine mixer → local fallback still authoritative.
    p.wireEngineMixer(nullptr, nullptr);
    EXPECT_NEAR(p.mixer().getBusGain(AudioBus::Sfx), 0.6f, kEps);
}

TEST(AudioPanelWire, EffectiveGainFollowsEngineMixerWhenWired)
{
    AudioPanel p;
    AudioMixer engineMixer;
    engineMixer.setBusGain(AudioBus::Master, 0.5f);
    engineMixer.setBusGain(AudioBus::Music,  0.8f);
    p.wireEngineMixer(&engineMixer, nullptr);

    // computeEffectiveSourceGain reads through the engine mixer:
    // master × music = 0.5 × 0.8 = 0.40.
    EXPECT_NEAR(p.computeEffectiveSourceGain(1, AudioBus::Music),
                0.5f * 0.8f, kEps);

    // Mutating the engine mixer externally — e.g. through the
    // AudioMixerApplySink wired to SettingsEditor — is visible to
    // the panel on the very next read.
    engineMixer.setBusGain(AudioBus::Music, 0.2f);
    EXPECT_NEAR(p.computeEffectiveSourceGain(1, AudioBus::Music),
                0.5f * 0.2f, kEps);
}

TEST(AudioPanelWire, SettingsEditorPointerStoredWithoutMixer)
{
    // Wiring a SettingsEditor without a mixer is tolerated — the
    // slider path only routes through Settings when *both* pointers
    // are non-null, so unintended-but-tolerated combinations stay
    // consistent rather than crashing.
    AudioPanel p;
    Settings s;
    SettingsEditor::ApplyTargets targets; // all null sinks
    SettingsEditor editor(s, targets);
    p.wireEngineMixer(nullptr, &editor);
    // Panel's local mixer still takes edits.
    p.mixer().setBusGain(AudioBus::Voice, 0.4f);
    EXPECT_NEAR(p.mixer().getBusGain(AudioBus::Voice), 0.4f, kEps);
}

// ---- Phase 10.9 P3 — engine ducking wire ---------------------------

TEST(AudioPanelWire, DuckingStateReadsThroughEnginePointer_P3)
{
    // Wiring a non-null DuckingState pointer makes the panel read
    // and write through the engine-owned authoritative state so the
    // Debug-tab preview matches what AudioSystem actually publishes
    // to AL_GAIN.
    AudioPanel p;
    DuckingState  engineState;
    DuckingParams engineParams;
    p.wireEngineDucking(&engineState, &engineParams);

    // Mutating through the panel accessor must reach the engine state.
    p.duckingState().triggered = true;
    EXPECT_TRUE(engineState.triggered);

    // Mutating the engine state directly must be visible through the panel.
    engineState.currentGain = 0.42f;
    EXPECT_NEAR(p.duckingState().currentGain, 0.42f, kEps);
}

TEST(AudioPanelWire, EffectiveGainUsesEngineDuckingWhenWired_P3)
{
    // computeEffectiveSourceGain pulls the duck from duckingState(),
    // which now prefers the engine pointer when wired. Setting the
    // engine state to 0.5 must dim the effective gain by 0.5.
    AudioPanel p;
    AudioMixer engineMixer;  // all-1 defaults
    DuckingState engineState;
    DuckingParams engineParams;
    Settings s;
    SettingsEditor::ApplyTargets targets;
    SettingsEditor editor(s, targets);
    p.wireEngineMixer(&engineMixer, &editor);
    p.wireEngineDucking(&engineState, &engineParams);

    engineState.currentGain = 0.5f;
    EXPECT_NEAR(p.computeEffectiveSourceGain(1, AudioBus::Music),
                0.5f, kEps);
}

TEST(AudioPanelWire, NullDuckingWireKeepsLocalFallback_P3)
{
    // Passing nullptrs keeps the panel on its local DuckingState —
    // standalone editor / test usage path.
    AudioPanel p;
    p.wireEngineDucking(nullptr, nullptr);
    p.duckingState().currentGain = 0.7f;
    EXPECT_NEAR(p.duckingState().currentGain, 0.7f, kEps);
}
