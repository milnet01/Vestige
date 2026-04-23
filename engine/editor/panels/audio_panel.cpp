// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_panel.cpp
/// @brief AudioPanel — editor UI over the Phase 10 audio pipeline.
#include "editor/panels/audio_panel.h"

#include "audio/audio_attenuation.h"
#include "audio/audio_engine.h"
#include "audio/audio_hrtf.h"
#include "audio/audio_source_component.h"
#include "core/settings.h"
#include "core/settings_editor.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "systems/audio_system.h"

#include <imgui.h>

#include <algorithm>

namespace Vestige
{

// ----- State mutators (ImGui-free, unit-testable) ----------------

int AudioPanel::addReverbZone(const ReverbZoneInstance& zone)
{
    m_reverbZones.push_back(zone);
    return static_cast<int>(m_reverbZones.size()) - 1;
}

bool AudioPanel::removeReverbZone(int index)
{
    if (index < 0 || index >= static_cast<int>(m_reverbZones.size()))
    {
        return false;
    }
    m_reverbZones.erase(m_reverbZones.begin() + index);
    if (m_selectedReverbZone == index)
    {
        m_selectedReverbZone = -1;
    }
    else if (m_selectedReverbZone > index)
    {
        // Shift the selection down to keep it pointing at the same zone.
        m_selectedReverbZone--;
    }
    return true;
}

int AudioPanel::addAmbientZone(const AmbientZoneInstance& zone)
{
    m_ambientZones.push_back(zone);
    return static_cast<int>(m_ambientZones.size()) - 1;
}

bool AudioPanel::removeAmbientZone(int index)
{
    if (index < 0 || index >= static_cast<int>(m_ambientZones.size()))
    {
        return false;
    }
    m_ambientZones.erase(m_ambientZones.begin() + index);
    if (m_selectedAmbientZone == index)
    {
        m_selectedAmbientZone = -1;
    }
    else if (m_selectedAmbientZone > index)
    {
        m_selectedAmbientZone--;
    }
    return true;
}

void AudioPanel::setSourceMuted(std::uint32_t entityId, bool muted)
{
    if (muted)
    {
        m_mutedSources.insert(entityId);
    }
    else
    {
        m_mutedSources.erase(entityId);
    }
}

bool AudioPanel::isSourceMuted(std::uint32_t entityId) const
{
    return m_mutedSources.count(entityId) != 0;
}

void AudioPanel::setSourceSoloed(std::uint32_t entityId, bool soloed)
{
    if (soloed)
    {
        m_soloedSources.insert(entityId);
    }
    else
    {
        m_soloedSources.erase(entityId);
    }
}

bool AudioPanel::isSourceSoloed(std::uint32_t entityId) const
{
    return m_soloedSources.count(entityId) != 0;
}

float AudioPanel::computeEffectiveSourceGain(std::uint32_t entityId, AudioBus bus) const
{
    if (isSourceMuted(entityId))
    {
        return 0.0f;
    }
    if (hasAnySoloedSource() && !isSourceSoloed(entityId))
    {
        return 0.0f;
    }
    // Read through mixer() so the effective gain follows the engine
    // mixer when wired, or the local fallback otherwise.
    const float bus_ = effectiveBusGain(mixer(), bus);
    const float duck = std::max(0.0f, std::min(1.0f, m_duckingState.currentGain));
    return std::max(0.0f, std::min(1.0f, bus_ * duck));
}

// ----- ImGui draw path -------------------------------------------

void AudioPanel::draw(AudioSystem* audioSystem, Scene* scene)
{
    if (!m_open) return;

    if (!ImGui::Begin("Audio", &m_open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("AudioTabs"))
    {
        if (ImGui::BeginTabItem("Mixer"))
        {
            drawMixerTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sources"))
        {
            drawSourcesTab(scene);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Zones"))
        {
            drawZonesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug"))
        {
            drawDebugTab(audioSystem);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void AudioPanel::drawMixerTab()
{
    ImGui::TextUnformatted("Bus gains");
    ImGui::Separator();

    static const std::pair<AudioBus, const char*> kBuses[] = {
        {AudioBus::Master,  "Master"},
        {AudioBus::Music,   "Music"},
        {AudioBus::Voice,   "Voice"},
        {AudioBus::Sfx,     "Sfx"},
        {AudioBus::Ambient, "Ambient"},
        {AudioBus::Ui,      "UI"},
    };

    // Phase 10.7 slice A3: when wired to SettingsEditor + engine
    // mixer, bus-gain edits flow through the Settings persistence
    // layer. SettingsEditor::mutate triggers AudioMixerApplySink,
    // which writes the engine-owned mixer; the panel's next frame
    // reads the updated value straight from the engine. In the
    // pre-wire fallback path (tests, standalone usage) the panel
    // mutates its own local mixer directly.
    AudioMixer& activeMixer = mixer();
    for (const auto& [bus, label] : kBuses)
    {
        // Phase 10 slice 13.3: route through setBusGain so the
        // [0, 1] clamp policy lives in exactly one place. ImGui's
        // SliderFloat clamps the UI input to the same range anyway,
        // but the pass-through is stylistically consistent with the
        // Settings apply layer.
        float g = activeMixer.getBusGain(bus);
        if (ImGui::SliderFloat(label, &g, 0.0f, 1.0f, "%.2f"))
        {
            if (m_settingsEditor != nullptr && m_engineMixer != nullptr)
            {
                const std::size_t idx = static_cast<std::size_t>(bus);
                m_settingsEditor->mutate([idx, g](Settings& s)
                {
                    s.audio.busGains[idx] = g;
                });
            }
            else
            {
                activeMixer.setBusGain(bus, g);
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Dialogue ducking");
    ImGui::Checkbox("Ducked (trigger)", &m_duckingState.triggered);
    ImGui::SliderFloat("Attack (s)",    &m_duckingParams.attackSeconds,  0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Release (s)",   &m_duckingParams.releaseSeconds, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Duck floor",    &m_duckingParams.duckFactor,     0.0f, 1.0f, "%.2f");
    ImGui::Text("Current gain: %.2f", static_cast<double>(m_duckingState.currentGain));
}

void AudioPanel::drawSourcesTab(Scene* scene)
{
    if (!scene)
    {
        ImGui::TextDisabled("No scene — source list unavailable.");
        return;
    }

    ImGui::TextUnformatted("Active audio sources:");
    ImGui::Separator();

    bool any = false;
    scene->forEachEntity([&](Entity& entity)
    {
        auto* src = entity.getComponent<AudioSourceComponent>();
        if (!src) return;

        any = true;
        const std::uint32_t id = entity.getId();

        ImGui::PushID(static_cast<int>(id));
        ImGui::Text("%s", entity.getName().c_str());
        ImGui::SameLine();
        bool muted = isSourceMuted(id);
        if (ImGui::Checkbox("Mute", &muted))
        {
            setSourceMuted(id, muted);
        }
        ImGui::SameLine();
        bool soloed = isSourceSoloed(id);
        if (ImGui::Checkbox("Solo", &soloed))
        {
            setSourceSoloed(id, soloed);
        }

        ImGui::Indent();
        ImGui::SliderFloat("Volume", &src->volume, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Pitch",  &src->pitch,  0.1f, 4.0f, "%.2f");
        ImGui::InputFloat("Min distance", &src->minDistance);
        ImGui::InputFloat("Max distance", &src->maxDistance);
        ImGui::Text("Attenuation: %s",
                    attenuationModelLabel(src->attenuationModel));
        ImGui::Unindent();
        ImGui::Separator();
        ImGui::PopID();
    });
    if (!any)
    {
        ImGui::TextDisabled("No AudioSourceComponent in scene.");
    }
}

void AudioPanel::drawZonesTab()
{
    // --- Reverb ---
    ImGui::TextUnformatted("Reverb zones");
    if (ImGui::Button("Add reverb zone"))
    {
        addReverbZone(ReverbZoneInstance{});
        m_selectedReverbZone = static_cast<int>(m_reverbZones.size()) - 1;
    }
    ImGui::Separator();

    for (std::size_t i = 0; i < m_reverbZones.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        const bool isSelected = (static_cast<int>(i) == m_selectedReverbZone);
        if (ImGui::Selectable(m_reverbZones[i].name.c_str(), isSelected))
        {
            m_selectedReverbZone = static_cast<int>(i);
        }
        ImGui::PopID();
    }

    if (m_selectedReverbZone >= 0 &&
        m_selectedReverbZone < static_cast<int>(m_reverbZones.size()))
    {
        ImGui::Separator();
        auto& z = m_reverbZones[static_cast<std::size_t>(m_selectedReverbZone)];
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", z.name.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        {
            z.name = nameBuf;
        }
        ImGui::DragFloat3("Center", &z.center.x, 0.1f);
        ImGui::SliderFloat("Core radius",  &z.coreRadius,  0.1f, 100.0f, "%.1f");
        ImGui::SliderFloat("Falloff band", &z.falloffBand, 0.0f, 100.0f, "%.1f");
        const char* presets[] = {"Generic", "SmallRoom", "LargeHall",
                                  "Cave", "Outdoor", "Underwater"};
        int presetIdx = static_cast<int>(z.preset);
        if (ImGui::Combo("Preset", &presetIdx, presets, IM_ARRAYSIZE(presets)))
        {
            z.preset = static_cast<ReverbPreset>(presetIdx);
        }
        if (ImGui::Button("Remove reverb zone"))
        {
            removeReverbZone(m_selectedReverbZone);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Ambient zones");
    if (ImGui::Button("Add ambient zone"))
    {
        addAmbientZone(AmbientZoneInstance{});
        m_selectedAmbientZone = static_cast<int>(m_ambientZones.size()) - 1;
    }
    ImGui::Separator();
    for (std::size_t i = 0; i < m_ambientZones.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i + 10000));
        const bool isSelected = (static_cast<int>(i) == m_selectedAmbientZone);
        if (ImGui::Selectable(m_ambientZones[i].name.c_str(), isSelected))
        {
            m_selectedAmbientZone = static_cast<int>(i);
        }
        ImGui::PopID();
    }

    if (m_selectedAmbientZone >= 0 &&
        m_selectedAmbientZone < static_cast<int>(m_ambientZones.size()))
    {
        ImGui::Separator();
        auto& z = m_ambientZones[static_cast<std::size_t>(m_selectedAmbientZone)];
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", z.name.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        {
            z.name = nameBuf;
        }
        ImGui::DragFloat3("Center", &z.center.x, 0.1f);
        char clipBuf[256];
        std::snprintf(clipBuf, sizeof(clipBuf), "%s", z.params.clipPath.c_str());
        if (ImGui::InputText("Clip path", clipBuf, sizeof(clipBuf)))
        {
            z.params.clipPath = clipBuf;
        }
        ImGui::SliderFloat("Core radius",  &z.params.coreRadius,  0.1f, 100.0f, "%.1f");
        ImGui::SliderFloat("Falloff band", &z.params.falloffBand, 0.0f, 100.0f, "%.1f");
        ImGui::SliderFloat("Max volume",   &z.params.maxVolume,   0.0f, 1.0f,   "%.2f");
        ImGui::InputInt("Priority", &z.params.priority);
        if (ImGui::Button("Remove ambient zone"))
        {
            removeAmbientZone(m_selectedAmbientZone);
        }
    }
}

void AudioPanel::drawDebugTab(AudioSystem* audioSystem)
{
    ImGui::Checkbox("Show zone overlays in viewport", &m_showZoneOverlay);
    ImGui::Separator();

    if (!audioSystem)
    {
        ImGui::TextDisabled("AudioSystem not registered.");
        return;
    }

    const auto& engine = audioSystem->getAudioEngine();
    if (!engine.isAvailable())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           "Audio hardware unavailable (silent mode).");
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f),
                       "Audio: available");

    ImGui::Text("Distance model: %s",
                attenuationModelLabel(engine.getDistanceModel()));
    ImGui::Text("Doppler factor: %.2f",
                static_cast<double>(engine.getDopplerParams().dopplerFactor));
    ImGui::Text("Speed of sound: %.1f m/s",
                static_cast<double>(engine.getDopplerParams().speedOfSound));

    ImGui::Separator();
    ImGui::TextUnformatted("HRTF");
    const auto& hrtf = engine.getHrtfSettings();
    ImGui::Text("Mode: %s",     hrtfModeLabel(hrtf.mode));
    ImGui::Text("Status: %s",   hrtfStatusLabel(engine.getHrtfStatus()));
    ImGui::Text("Dataset: %s",
                hrtf.preferredDataset.empty() ? "<driver default>"
                                               : hrtf.preferredDataset.c_str());

    const auto datasets = engine.getAvailableHrtfDatasets();
    if (!datasets.empty())
    {
        ImGui::TextUnformatted("Available datasets:");
        for (const auto& name : datasets)
        {
            ImGui::BulletText("%s", name.c_str());
        }
    }
    else
    {
        ImGui::TextDisabled("No HRTF datasets enumerated by driver.");
    }
}

} // namespace Vestige
