// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_panel.h
/// @brief Editor panel for the Phase 10 audio pipeline — mixer
///        buses, reverb / ambient zone placement, source mute/solo,
///        and debug visualisations over the live AudioEngine state.
#pragma once

#include "audio/audio_ambient.h"
#include "audio/audio_mixer.h"
#include "audio/audio_reverb.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace Vestige
{

class AudioSystem;
class Scene;

/// @brief Editor panel for audio placement, mixing, and debugging.
///
/// Hosts four tabs:
///   - **Mixer**  — per-bus gains and the dialogue-duck trigger.
///   - **Sources** — active `AudioSourceComponent`s in the scene
///                   with per-entity mute / solo toggles.
///   - **Zones**   — reverb-zone painting + ambient-zone placement.
///                   Lists are editor-draft staging; the scene
///                   serialiser will read them when the placement
///                   surfaces land in the runtime zone systems.
///   - **Debug**   — voice-pool utilisation, HRTF status + dataset
///                   selector, distance-model picker, and an
///                   overlay toggle so the viewport can draw each
///                   zone's falloff sphere.
///
/// The ImGui draw path is intentionally straightforward. All
/// non-GL state (mixer, ducking, zone lists, selection, mute/solo
/// sets, overlay toggles) is exposed through getters so unit tests
/// can exercise the panel without an ImGui context — matching the
/// `NavigationPanel` pattern.
class AudioPanel
{
public:
    /// @brief Editor-draft reverb zone — position + geometry +
    ///        preset. Moves to a scene-owned component once the
    ///        runtime reverb placement surface lands.
    struct ReverbZoneInstance
    {
        std::string  name        = "Reverb Zone";
        glm::vec3    center      = glm::vec3(0.0f);
        float        coreRadius  = 5.0f;
        float        falloffBand = 2.0f;
        ReverbPreset preset      = ReverbPreset::Generic;
    };

    /// @brief Editor-draft ambient zone — position + the full
    ///        `AmbientZone` params the runtime will consume.
    struct AmbientZoneInstance
    {
        std::string name   = "Ambient Zone";
        glm::vec3   center = glm::vec3(0.0f);
        AmbientZone params;
    };

    /// @brief Draws the panel inside its own ImGui window. `scene`
    ///        and `audioSystem` may be null — the panel disables
    ///        the matching tab when either is missing.
    void draw(AudioSystem* audioSystem, Scene* scene);

    // -- Open/close -------------------------------------------------

    bool isOpen() const { return m_open; }
    void setOpen(bool open) { m_open = open; }
    void toggleOpen() { m_open = !m_open; }

    // -- Mixer + ducking -------------------------------------------

    AudioMixer&          mixer()                { return m_mixer; }
    const AudioMixer&    mixer() const          { return m_mixer; }
    DuckingState&        duckingState()         { return m_duckingState; }
    const DuckingState&  duckingState() const   { return m_duckingState; }
    DuckingParams&       duckingParams()        { return m_duckingParams; }
    const DuckingParams& duckingParams() const  { return m_duckingParams; }

    // -- Reverb zones (editor draft) -------------------------------

    const std::vector<ReverbZoneInstance>& reverbZones() const { return m_reverbZones; }

    /// @brief Appends a zone and returns its index.
    int addReverbZone(const ReverbZoneInstance& zone);

    /// @brief Removes a zone by index. Returns true on success.
    ///        If the removed zone was the current selection, the
    ///        selection falls back to -1.
    bool removeReverbZone(int index);

    int  selectedReverbZone() const       { return m_selectedReverbZone; }
    void selectReverbZone(int index)      { m_selectedReverbZone = index; }

    // -- Ambient zones (editor draft) ------------------------------

    const std::vector<AmbientZoneInstance>& ambientZones() const { return m_ambientZones; }

    int  addAmbientZone(const AmbientZoneInstance& zone);
    bool removeAmbientZone(int index);

    int  selectedAmbientZone() const      { return m_selectedAmbientZone; }
    void selectAmbientZone(int index)     { m_selectedAmbientZone = index; }

    // -- Per-source mute / solo ------------------------------------

    void setSourceMuted(std::uint32_t entityId, bool muted);
    bool isSourceMuted(std::uint32_t entityId) const;
    void setSourceSoloed(std::uint32_t entityId, bool soloed);
    bool isSourceSoloed(std::uint32_t entityId) const;
    bool hasAnySoloedSource() const { return !m_soloedSources.empty(); }

    // -- Debug overlay ---------------------------------------------

    bool isZoneOverlayEnabled() const       { return m_showZoneOverlay; }
    void setZoneOverlayEnabled(bool on)     { m_showZoneOverlay = on; }

    /// @brief Returns the effective playback gain for a source,
    ///        taking its bus routing and the mute/solo state into
    ///        account. Intended for both the ImGui meters and the
    ///        engine-side AudioSystem when it consults this panel.
    ///
    /// Rules:
    ///   - If any source is soloed and this one isn't → gain 0.
    ///   - If this source is muted → gain 0.
    ///   - Otherwise → `effectiveBusGain(mixer, bus) *
    ///                 duckingState.currentGain` (clamped to [0, 1]).
    float computeEffectiveSourceGain(std::uint32_t entityId, AudioBus bus) const;

private:
    void drawMixerTab();
    void drawSourcesTab(Scene* scene);
    void drawZonesTab();
    void drawDebugTab(AudioSystem* audioSystem);

    bool m_open = false;

    AudioMixer    m_mixer{};
    DuckingState  m_duckingState{};
    DuckingParams m_duckingParams{};

    std::vector<ReverbZoneInstance>  m_reverbZones;
    std::vector<AmbientZoneInstance> m_ambientZones;
    int m_selectedReverbZone  = -1;
    int m_selectedAmbientZone = -1;

    std::unordered_set<std::uint32_t> m_mutedSources;
    std::unordered_set<std::uint32_t> m_soloedSources;

    bool m_showZoneOverlay = false;
};

} // namespace Vestige
