// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_panel.cpp
/// @brief AudioPanel — editor UI over the Phase 10 audio pipeline.
#include "editor/panels/audio_panel.h"

#include "audio/acoustic_baker.h"
#include "audio/acoustic_probe_component.h"
#include "audio/audio_attenuation.h"
#include "audio/audio_engine.h"
#include "audio/audio_mix_monitor.h"
#include "audio/audio_spectrum.h"
#include "audio/audio_hrtf.h"
#include "audio/audio_output_mode.h"
#include "audio/audio_source_component.h"
#include "audio/reverb_zone_component.h"
#include "core/settings.h"
#include "core/settings_editor.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "systems/audio_system.h"
#include "systems/reverb_system.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Vestige
{

// ----- State mutators (ImGui-free, unit-testable) ----------------

Entity* AudioPanel::createReverbZone(Scene& scene)
{
    Entity* entity = scene.createEntity("Reverb Zone");
    entity->addComponent<ReverbZoneComponent>();
    m_selectedReverbZoneEntity = entity->getId();
    return entity;
}

bool AudioPanel::removeReverbZone(Scene& scene, std::uint32_t entityId)
{
    const bool removed = scene.removeEntity(entityId);
    if (removed && m_selectedReverbZoneEntity == entityId)
    {
        m_selectedReverbZoneEntity = 0;
    }
    return removed;
}

Entity* AudioPanel::createAcousticProbe(Scene& scene)
{
    Entity* entity = scene.createEntity("Acoustic Probe");
    entity->addComponent<AcousticProbeComponent>();
    m_selectedAcousticProbeEntity = entity->getId();
    return entity;
}

bool AudioPanel::removeAcousticProbe(Scene& scene, std::uint32_t entityId)
{
    const bool removed = scene.removeEntity(entityId);
    if (removed && m_selectedAcousticProbeEntity == entityId)
    {
        m_selectedAcousticProbeEntity = 0;
    }
    return removed;
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
    const float duck = std::max(0.0f, std::min(1.0f, duckingState().currentGain));
    return std::max(0.0f, std::min(1.0f, bus_ * duck));
}

// ----- ImGui draw path -------------------------------------------

void AudioPanel::draw(AudioSystem* audioSystem, Scene* scene)
{
    // AX12: reset the Debug-tab activation flag before any early return, so the
    // editor's post-draw poll deactivates the mix monitor on every exit path
    // (window closed / collapsed / another tab selected).
    m_debugTabActive = false;

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
            drawZonesTab(scene);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug"))
        {
            m_debugTabActive = true;  // AX12 activation poll
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
    ImGui::Checkbox("Ducked (trigger)", &duckingState().triggered);
    ImGui::SliderFloat("Attack (s)",    &duckingParams().attackSeconds,  0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Release (s)",   &duckingParams().releaseSeconds, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Duck floor",    &duckingParams().duckFactor,     0.0f, 1.0f, "%.2f");
    ImGui::Text("Current gain: %.2f", static_cast<double>(duckingState().currentGain));
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

void AudioPanel::drawZonesTab(Scene* scene)
{
    // --- Reverb (real scene ReverbZoneComponent entities, AX2 R4) ---
    ImGui::TextUnformatted("Reverb zones");
    if (!scene)
    {
        ImGui::TextDisabled("No scene — reverb zones unavailable.");
    }
    else
    {
        if (ImGui::Button("Add reverb zone"))
        {
            createReverbZone(*scene);
        }
        ImGui::Separator();

        // List every entity carrying a ReverbZoneComponent (mirrors the
        // Sources tab's scene walk). Selection is by entity id.
        scene->forEachEntity([&](Entity& entity)
        {
            if (entity.getComponent<ReverbZoneComponent>() == nullptr)
            {
                return;
            }
            const std::uint32_t id = entity.getId();
            ImGui::PushID(static_cast<int>(id));
            const bool isSelected = (id == m_selectedReverbZoneEntity);
            if (ImGui::Selectable(entity.getName().c_str(), isSelected))
            {
                m_selectedReverbZoneEntity = id;
            }
            ImGui::PopID();
        });

        Entity* selected = scene->findEntityById(m_selectedReverbZoneEntity);
        ReverbZoneComponent* z =
            selected ? selected->getComponent<ReverbZoneComponent>() : nullptr;
        if (z != nullptr)
        {
            ImGui::Separator();
            ImGui::PushID(static_cast<int>(m_selectedReverbZoneEntity));
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s",
                          selected->getName().c_str());
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            {
                selected->setName(nameBuf);
            }
            // Position is the entity transform — set it via the scene
            // hierarchy / gizmo like any entity, not here.
            ImGui::SliderFloat("Core radius",  &z->coreRadius,  0.1f, 100.0f, "%.1f");
            ImGui::SliderFloat("Falloff band", &z->falloffBand, 0.0f, 100.0f, "%.1f");
            const char* presets[] = {"Generic", "SmallRoom", "LargeHall",
                                      "Cave", "Outdoor", "Underwater"};
            int presetIdx = static_cast<int>(z->preset);
            if (ImGui::Combo("Preset", &presetIdx, presets, IM_ARRAYSIZE(presets)))
            {
                z->preset = static_cast<ReverbPreset>(presetIdx);
            }
            char irBuf[256];
            std::snprintf(irBuf, sizeof(irBuf), "%s", z->irPath.c_str());
            if (ImGui::InputText("IR path (convolution)", irBuf, sizeof(irBuf)))
            {
                z->irPath = irBuf;
            }
            ImGui::SliderFloat("Wet gain", &z->wetGain, 0.0f, 1.0f, "%.2f");
            if (ImGui::Button("Remove reverb zone"))
            {
                removeReverbZone(*scene, m_selectedReverbZoneEntity);
            }
            ImGui::PopID();
        }
    }

    // --- Acoustic probes + offline bake (AX3 B5) ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Acoustic probes (baked reverb)");
    if (!scene)
    {
        ImGui::TextDisabled("No scene — acoustic probes unavailable.");
    }
    else
    {
        if (ImGui::Button("Add acoustic probe"))
        {
            createAcousticProbe(*scene);
        }
        ImGui::Separator();

        scene->forEachEntity([&](Entity& entity)
        {
            if (entity.getComponent<AcousticProbeComponent>() == nullptr)
            {
                return;
            }
            const std::uint32_t id = entity.getId();
            ImGui::PushID(static_cast<int>(id + 20000));
            const bool isSelected = (id == m_selectedAcousticProbeEntity);
            if (ImGui::Selectable(entity.getName().c_str(), isSelected))
            {
                m_selectedAcousticProbeEntity = id;
            }
            ImGui::PopID();
        });

        Entity* selected = scene->findEntityById(m_selectedAcousticProbeEntity);
        AcousticProbeComponent* probe =
            selected ? selected->getComponent<AcousticProbeComponent>() : nullptr;
        if (probe != nullptr)
        {
            ImGui::Separator();
            ImGui::PushID(static_cast<int>(m_selectedAcousticProbeEntity + 20000));
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", selected->getName().c_str());
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            {
                selected->setName(nameBuf);
            }
            // Position is the entity transform (gizmo), like a reverb zone.
            ImGui::SliderFloat("Influence radius", &probe->influenceRadius,
                               0.1f, 100.0f, "%.1f");
            // Baked IR path is written by the bake, not hand-edited — show it.
            ImGui::Text("Baked IR: %s",
                        probe->bakedIrPath.empty() ? "(not baked)"
                                                   : probe->bakedIrPath.c_str());
            if (ImGui::Button("Remove acoustic probe"))
            {
                removeAcousticProbe(*scene, m_selectedAcousticProbeEntity);
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        // The bake needs the live ReverbSystem (physics + job system + scene
        // path). Disabled in tests / standalone where it is not wired.
        if (m_reverbSystem == nullptr)
        {
            ImGui::TextDisabled("Bake Acoustics — unavailable (no reverb system).");
        }
        else if (ImGui::Button("Bake Acoustics"))
        {
            const AcousticBakeResult r = m_reverbSystem->bakeAcoustics(*scene);
            m_haveLastBake       = true;
            m_lastBakeOk         = r.ok;
            m_lastBakeProbeCount = r.probes.size();
            m_lastBakeFacetCount = r.facets.size();
            m_lastBakeVolumeM3   = r.roomVolumeM3;
        }
        if (m_haveLastBake)
        {
            if (m_lastBakeOk)
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f),
                                   "Baked %zu probe(s), %zu facet(s), %.0f m^3",
                                   m_lastBakeProbeCount, m_lastBakeFacetCount,
                                   static_cast<double>(m_lastBakeVolumeM3));
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "Bake produced nothing — save the scene and add "
                                   "probes + static geometry (see log).");
            }
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

void AudioPanel::drawSpectrumViewer(AudioSystem* audioSystem)
{
    AudioEngine& engine  = audioSystem->getAudioEngine();
    AudioMixMonitor& mon = engine.mixMonitor();
    mon.flushFrame();  // once per frame, after all producer systems have updated

    AudioMixer& mx = mixer();

    // Bus select / solo row. Only Music & Sfx carry an analysis signal (§1.2);
    // the rest are file-decoded (greyed). Clicking the active solo clears it.
    struct BusBtn { const char* label; AudioBus bus; };
    static const BusBtn kBuses[] = {
        {"Music", AudioBus::Music}, {"Voice", AudioBus::Voice},
        {"Sfx",   AudioBus::Sfx},   {"Ambient", AudioBus::Ambient},
        {"Ui",    AudioBus::Ui}};
    ImGui::TextUnformatted("Solo / view:");
    for (const BusBtn& b : kBuses)
    {
        ImGui::SameLine();
        const bool graphed = isMixMonitorGraphedBus(b.bus);
        const bool soloed  = mx.soloBus == static_cast<int>(b.bus);
        if (!graphed) ImGui::BeginDisabled();
        if (soloed)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.90f, 1.0f));
        if (ImGui::Button(b.label))
            mx.soloBus = soloed ? -1 : static_cast<int>(b.bus);
        if (soloed) ImGui::PopStyleColor();
        if (!graphed)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("file-decoded — not graphed");
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Freeze waveform", &m_freezeWaveform);

    // Displayed bus = the soloed bus when it's graphed, else Music.
    AudioBus shown = AudioBus::Music;
    if (mx.soloBus >= 0 && isMixMonitorGraphedBus(static_cast<AudioBus>(mx.soloBus)))
        shown = static_cast<AudioBus>(mx.soloBus);

    const std::vector<float>& ring = mon.ring(shown);
    const int  rate = std::max(1, mon.rateHz(shown));
    const bool live = mon.hadRecentSignal(shown);

    ImGui::TextDisabled("Analysis mix (CPU content, pre-spatialization) — %s%s",
                        audioBusLabel(shown),
                        shown == AudioBus::Music ? "; leads speakers by decode-ahead"
                                                 : " (procedural only)");

    // ---- Spectrum: last WINDOW samples (front-padded), dB + ballistics ----
    std::vector<float> blk(kMixMonitorWindow, 0.0f);
    if (!ring.empty())
    {
        const std::size_t n = std::min(ring.size(), kMixMonitorWindow);
        std::copy(ring.end() - static_cast<std::ptrdiff_t>(n), ring.end(),
                  blk.end() - static_cast<std::ptrdiff_t>(n));
    }
    std::vector<float> mags;
    computeMagnitudeSpectrum(blk.data(), kMixMonitorWindow, mags);
    if (m_specDb.size() != mags.size())
        m_specDb.assign(mags.size(), -80.0f);
    std::vector<float> freq(mags.size());
    std::vector<float> db(mags.size());
    std::size_t peak = 0;
    for (std::size_t i = 0; i < mags.size(); ++i)
    {
        float d = 20.0f * std::log10(std::max(mags[i], 1e-9f));
        d = std::max(d, -80.0f);
        // Fast-attack / slow-release ballistics — UI-only, never fed back (INV-8).
        m_specDb[i] = (d > m_specDb[i]) ? d : (0.9f * m_specDb[i] + 0.1f * d);
        freq[i] = static_cast<float>(i) * static_cast<float>(rate) /
                  static_cast<float>(kMixMonitorWindow);
        db[i] = m_specDb[i];
        if (i > 0 && db[i] > db[peak]) peak = i;
    }
    if (ImPlot::BeginPlot("##ax12_spectrum", ImVec2(-1, 140),
                          ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText))
    {
        ImPlot::SetupAxes("Hz", "dB", ImPlotAxisFlags_NoGridLines, 0);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        ImPlot::SetupAxisLimits(ImAxis_X1, 20.0, 20000.0);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -80.0, 0.0);
        if (!freq.empty())
        {
            ImPlotSpec spec;
            spec.FillAlpha = 0.4f;
            ImPlot::PlotShaded("spectrum", freq.data(), db.data(),
                               static_cast<int>(freq.size()), -80.0, spec);
            ImPlot::PlotLine("spectrum_top", freq.data(), db.data(),
                             static_cast<int>(freq.size()));
        }
        ImPlot::EndPlot();
    }
    if (!freq.empty())
        ImGui::Text("peak: %.0f Hz  %.1f dB%s", static_cast<double>(freq[peak]),
                    static_cast<double>(db[peak]), live ? "" : "   (idle)");

    // ---- Waveform: min/max envelope over the last HISTORY_SECONDS ----
    constexpr std::size_t kWavePoints = 512;
    if (!m_freezeWaveform)
    {
        const std::size_t want =
            static_cast<std::size_t>(kMixMonitorHistorySeconds *
                                     static_cast<float>(rate));
        const std::size_t have = std::min(ring.size(), want);
        m_waveT.assign(kWavePoints, 0.0f);
        m_waveMin.assign(kWavePoints, 0.0f);
        m_waveMax.assign(kWavePoints, 0.0f);
        if (have > 0)
        {
            const std::size_t start = ring.size() - have;
            for (std::size_t p = 0; p < kWavePoints; ++p)
            {
                const std::size_t a = start + (have * p) / kWavePoints;
                const std::size_t e = start + (have * (p + 1)) / kWavePoints;
                float mn = 0.0f, mxv = 0.0f;
                bool first = true;
                for (std::size_t k = a; k < e && k < ring.size(); ++k)
                {
                    const float v = ring[k];
                    if (first) { mn = mxv = v; first = false; }
                    else { mn = std::min(mn, v); mxv = std::max(mxv, v); }
                }
                m_waveT[p]   = static_cast<float>(p) / static_cast<float>(kWavePoints) *
                               kMixMonitorHistorySeconds;
                m_waveMin[p] = mn;
                m_waveMax[p] = mxv;
            }
        }
    }
    if (ImPlot::BeginPlot("##ax12_wave", ImVec2(-1, 90), ImPlotFlags_CanvasOnly))
    {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations,
                          ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0,
                                static_cast<double>(kMixMonitorHistorySeconds),
                                ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, 1.0, ImPlotCond_Always);
        if (!m_waveT.empty())
        {
            ImPlotSpec wspec;
            wspec.FillAlpha = 0.5f;
            ImPlot::PlotShaded("wave", m_waveT.data(), m_waveMin.data(),
                               m_waveMax.data(), static_cast<int>(m_waveT.size()),
                               wspec);
        }
        ImPlot::EndPlot();
    }

    // Clip indicator — scan the full-rate content ring (not the decimation).
    bool clip = false;
    for (float v : ring)
    {
        if (v > 1.0f || v < -1.0f) { clip = true; break; }
    }
    if (clip)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "CLIP ( |sample| > 1.0 )");

    ImGui::Separator();
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

    // AX12 — live spectrum / waveform / per-bus solo (above the text status).
    drawSpectrumViewer(audioSystem);

    ImGui::Text("Distance model: %s",
                attenuationModelLabel(engine.getDistanceModel()));
    ImGui::Text("Doppler factor: %.2f",
                static_cast<double>(engine.getDopplerParams().dopplerFactor));
    ImGui::Text("Speed of sound: %.1f m/s",
                static_cast<double>(engine.getDopplerParams().speedOfSound));

    // AX8 — current speaker layout + whether the device can do surround.
    ImGui::Text("Speaker layout: %s", audioOutputLayoutLabel(engine.getOutputLayout()));
    if (engine.isSurroundOutputSupported())
    {
        ImGui::TextDisabled("ALC_SOFT_output_mode present (5.1/7.1 available)");
    }
    else
    {
        ImGui::TextDisabled("ALC_SOFT_output_mode absent (driver downmix only)");
    }

    // AX6 — air-absorption master toggle state.
    ImGui::Text("Air absorption: %s",
                engine.isAirAbsorptionEnabled() ? "on" : "off");
    // AX5 — audio LOD-ladder master toggle state.
    ImGui::Text("Audio LOD: %s", engine.isLodEnabled() ? "on" : "off");
    // AX4 S9 — procedural (synthesised) footstep / impact audio state.
    ImGui::Text("Procedural audio: %s",
                engine.isProceduralAudioEnabled() ? "on" : "off");
    ImGui::Text("Untagged-collision sound: %s",
                engine.emitUntaggedCollisions() ? "on" : "off");
    // AX13 — side-chain duck routes loaded from mix_graph.json (0 = the
    // global manual duck only).
    ImGui::Text("Side-chain duck routes: %zu",
                audioSystem->duckingRouter().routes().size());

    // AX2 R4 — reverb backend + live winning zone. Backend is "Dry" when the
    // slot is unavailable or the master toggle is off, else the init-chosen
    // backend. The winning zone + wet gain come from the live ReverbSystem
    // (null in standalone / test usage).
    const char* reverbBackend =
        !engine.isReverbAvailable()  ? "Dry (unavailable)" :
        !engine.isReverbEnabled()    ? "Dry (disabled)"    :
        engine.reverbBackend() == AudioEngine::ReverbBackend::Convolution
            ? "Convolution" : "Parametric";
    ImGui::Text("Reverb backend: %s", reverbBackend);
    if (m_reverbSystem != nullptr)
    {
        const std::string& zone = m_reverbSystem->winningZoneName();
        ImGui::Text("Active reverb zone: %s",
                    zone.empty() ? "<none>" : zone.c_str());
        ImGui::Text("Reverb wet gain: %.2f",
                    static_cast<double>(m_reverbSystem->currentWetGain()));
    }

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
