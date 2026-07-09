// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file reverb_system.cpp
/// @brief ReverbSystem implementation (AX2 R3 — zone selection + slot drive).
#include "systems/reverb_system.h"

#include "audio/acoustic_probe_component.h"
#include "audio/audio_engine.h"
#include "audio/audio_reverb.h"
#include "audio/audio_source_component.h"
#include "audio/reverb_zone_component.h"
#include "core/engine.h"
#include "core/logger.h"
#include "core/system_registry.h"
#include "systems/audio_system.h"
#include "scene/component.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Vestige
{

namespace
{
/// @brief Per-second slew rate for the slot wet gain — how fast the tail fades
///        in on entering a zone and out on leaving it. `clamp(dt*rate, 0, 1)`
///        makes the approach framerate-independent (mirrors AX1 occlusion's
///        `kOcclusionSlewPerSec`). ~8/s ⇒ ~63% of the way in ~125 ms:
///        inaudible-fast yet click-free.
constexpr float kReverbGainSlewPerSec = 8.0f;
} // namespace

std::vector<LoadedAcousticProbe> loadAcousticsIndex(const std::string& sceneSourcePath)
{
    std::vector<LoadedAcousticProbe> probes;
    if (sceneSourcePath.empty())
    {
        return probes;  // In-memory scene — no sidecar to read.
    }

    namespace fs = std::filesystem;
    const fs::path scenePath(sceneSourcePath);
    const fs::path sidecarDir =
        scenePath.parent_path() / (scenePath.stem().string() + "_acoustics");
    const fs::path indexPath = sidecarDir / "acoustics_index.json";

    std::error_code ec;
    if (!fs::exists(indexPath, ec))
    {
        return probes;  // Unbaked scene — caller falls back to authored zones.
    }

    std::ifstream in(indexPath, std::ios::binary);
    nlohmann::json j;
    try
    {
        in >> j;
    }
    catch (const std::exception& e)
    {
        Logger::warning("[ReverbSystem] acoustics index parse failed (" + indexPath.string()
                        + "): " + e.what());
        return probes;
    }

    if (!j.contains("probes") || !j["probes"].is_array())
    {
        return probes;
    }

    for (const auto& p : j["probes"])
    {
        if (!p.contains("position") || !p["position"].is_array() || p["position"].size() != 3 ||
            !p.contains("ir") || !p["ir"].is_string())
        {
            continue;  // Malformed entry — skip it, keep the rest.
        }

        const std::string irFile = p["ir"].get<std::string>();
        // The index stores a bare filename; a separator would let a hand-edited
        // index escape the sidecar dir. Reject it (loadReverbIr's sandbox is the
        // second line of defence, but keeping paths inside the dir is clearer).
        if (irFile.find('/') != std::string::npos || irFile.find('\\') != std::string::npos)
        {
            continue;
        }

        LoadedAcousticProbe probe;
        probe.position        = glm::vec3(p["position"][0].get<float>(),
                                          p["position"][1].get<float>(),
                                          p["position"][2].get<float>());
        probe.influenceRadius = p.value("influenceRadius", 10.0f);
        probe.irPath          = (sidecarDir / irFile).string();
        probe.id              = p.value("id", 0u);
        probes.push_back(std::move(probe));
    }

    if (!probes.empty())
    {
        Logger::info("[ReverbSystem] loaded " + std::to_string(probes.size())
                     + " baked acoustic probe(s) from " + indexPath.string());
    }
    return probes;
}

bool ReverbSystem::initialize(Engine& engine)
{
    m_engine = &engine;

    // All systems are registered before initializeAll(), so AudioSystem
    // resolves here; borrow its AudioEngine (a stable member reference — the
    // device comes up later in AudioSystem::initialize). Null only in test
    // harnesses with no AudioSystem, where update() then no-ops.
    if (AudioSystem* audioSys = engine.getSystemRegistry().getSystem<AudioSystem>())
    {
        m_audioEngine = &audioSys->getAudioEngine();
    }
    return true;
}

void ReverbSystem::shutdown()
{
    m_engine      = nullptr;
    m_audioEngine = nullptr;
}

void ReverbSystem::update(float deltaTime)
{
    if (m_engine == nullptr || m_audioEngine == nullptr)
    {
        return;  // No slot to drive.
    }

    Scene* scene = m_engine->getSceneManager().getActiveScene();
    if (scene == nullptr)
    {
        return;
    }

    // The listener is the camera (single-listener engine). PostCamera phase
    // guarantees the camera has been stepped this frame.
    const glm::vec3 listenerPos = m_engine->getCamera().getPosition();

    // --- B4: (re)load the scene's baked acoustic probes when it changes.
    // Cheap identity check per frame; the file read happens only on a switch or
    // an explicit invalidate (a re-bake).
    if (scene != m_bakedScene || scene->getSourcePath() != m_bakedScenePath)
    {
        m_bakedScene     = scene;
        m_bakedScenePath = scene->getSourcePath();
        m_bakedProbes    = loadAcousticsIndex(m_bakedScenePath);
        m_bakedProbePositions.clear();
        m_bakedProbePositions.reserve(m_bakedProbes.size());
        for (const LoadedAcousticProbe& p : m_bakedProbes)
        {
            m_bakedProbePositions.push_back(p.position);
        }
    }

    // A baked scene drives the slot from the nearest probe IR, in preference to
    // authored zones (§6.3). When it declines (reverb off, parametric backend,
    // or no bake) the authored-zone path below runs unchanged.
    if (driveFromBakedProbes(listenerPos, deltaTime, *scene))
    {
        return;
    }

    // --- Gather zones once; reused for listener selection and per-source send.
    struct GatheredZone
    {
        glm::vec3          center;
        float              coreRadius;
        float              falloffBand;
        ReverbParams       params;
        float              wetGain;
        const std::string* irPath;  // Borrows the component string; valid this
                                     // frame (the scene is not mutated below).
        const std::string* name;    // Borrows the entity name (Debug read-out).
    };
    std::vector<GatheredZone> zones;
    // AX2 R4 — the master toggle simply short-circuits the gather: an empty zone
    // set makes selectReverbZone report dry, so the slot slews to 0 and no source
    // gets a send. No separate "disabled" branch needed.
    if (m_audioEngine->isReverbEnabled())
    {
        scene->forEachEntity([&](Entity& entity)
        {
            auto* z = entity.getComponent<ReverbZoneComponent>();
            if (z == nullptr)
            {
                return;
            }
            zones.push_back({ entity.getWorldPosition(), z->coreRadius,
                              z->falloffBand, reverbPresetParams(z->preset),
                              z->wetGain, &z->irPath, &entity.getName() });
        });
    }

    // --- Pick the winning zone (+ neighbour) from the listener position.
    std::vector<ReverbZoneEval> evals;
    evals.reserve(zones.size());
    for (const GatheredZone& z : zones)
    {
        evals.push_back({ glm::length(z.center - listenerPos),
                          z.coreRadius, z.falloffBand, z.params, z.wetGain });
    }
    const ReverbSelection sel = selectReverbZone(evals);

    // Record the winner for the Debug tab (empty when dry / disabled). The
    // explicit size check both states the invariant (sel.winner indexes the
    // 1:1 `evals`/`zones` vectors) and satisfies cppcheck's value-flow, which
    // can't correlate `evals.size()` with `zones.size()` across the call.
    m_winningZoneName.clear();
    if (sel.winner >= 0 &&
        static_cast<std::size_t>(sel.winner) < zones.size())
    {
        m_winningZoneName = *zones[static_cast<std::size_t>(sel.winner)].name;
    }

    // --- Drive the slot: character (params or IR swap) + slewed wet gain.
    // AX2 R4 — cap the target at the accessibility ceiling before slewing.
    const float target =
        std::min(sel.targetWetGain, m_audioEngine->reverbWetCap());
    const float slew = std::clamp(deltaTime * kReverbGainSlewPerSec, 0.0f, 1.0f);
    m_slotWetGain = slewReverbWetGain(m_slotWetGain, target, slew);

    if (m_audioEngine->reverbBackend() == AudioEngine::ReverbBackend::Convolution)
    {
        // Convolution: snap to the winning zone's IR. On a change, dip the wet
        // gain to 0 this frame and let the slew ramp it back over the next few
        // — a glitch-free swap without a second slot (§5.2 step 3; a dual-slot
        // crossfade is the documented v2 refinement). An IR-less winning zone
        // simply leaves the last IR attached (nothing to swap to).
        if (sel.winner >= 0)
        {
            const std::string& winnerIr =
                *zones[static_cast<std::size_t>(sel.winner)].irPath;
            if (!winnerIr.empty() && winnerIr != m_attachedIrPath)
            {
                const unsigned int buf = m_audioEngine->loadReverbIr(winnerIr);
                if (buf != 0)
                {
                    m_audioEngine->attachReverbIr(buf);
                    m_attachedIrPath = winnerIr;
                    m_slotWetGain    = 0.0f;  // dip; slew ramps it back.
                }
            }
        }
    }
    else
    {
        // Parametric: push the blended room character each frame. Harmless when
        // unchanged; setReverbParams itself no-ops when there is no slot.
        m_audioEngine->setReverbParams(sel.blendedParams);
    }
    m_audioEngine->setReverbWetGain(m_slotWetGain);

    // --- Per-source send: unity for any spatial source inside any zone, else 0
    //     (v1 policy — audio_source_state.h). ReverbSystem writes reverbSend;
    //     AudioSystem's compose loop reads it the same frame (AX1 pattern).
    scene->forEachEntity([&](Entity& entity)
    {
        auto* s = entity.getComponent<AudioSourceComponent>();
        if (s == nullptr)
        {
            return;
        }
        float send = 0.0f;
        if (s->spatial)
        {
            const glm::vec3 sourcePos = entity.getWorldPosition();
            for (const GatheredZone& z : zones)
            {
                if (computeReverbZoneWeight(z.coreRadius, z.falloffBand,
                                            glm::length(z.center - sourcePos)) > 0.0f)
                {
                    send = 1.0f;
                    break;
                }
            }
        }
        s->reverbSend = send;
    });
}

bool ReverbSystem::driveFromBakedProbes(const glm::vec3& listenerPos, float deltaTime,
                                        Scene& scene)
{
    // Baked probes are IR-only, so they need the convolution backend; on a
    // parametric-only device (no AL_SOFTX_convolution_effect) fall back to the
    // authored-zone path. Reverb disabled or unbaked scene → defer too.
    if (!m_audioEngine->isReverbEnabled() ||
        m_audioEngine->reverbBackend() != AudioEngine::ReverbBackend::Convolution ||
        m_bakedProbes.empty())
    {
        return false;
    }

    // Nearest probe wins (the pure lookup shared with the placement/bake code).
    const int winner = nearestAcousticProbeIndex(m_bakedProbePositions, listenerPos);
    if (winner < 0)
    {
        return false;  // Defensive: non-empty above, so unreachable.
    }
    const LoadedAcousticProbe& probe = m_bakedProbes[static_cast<std::size_t>(winner)];
    const bool inside =
        glm::length(probe.position - listenerPos) <= probe.influenceRadius;

    // Snap-with-dip IR swap on a winner change: load + attach the new IR, then
    // dip the wet gain so the slew ramps it back over the next few frames —
    // click-free without a second slot (§5.2 step 3, same as the zone path).
    if (!probe.irPath.empty() && probe.irPath != m_attachedIrPath)
    {
        const unsigned int buf = m_audioEngine->loadReverbIr(probe.irPath);
        if (buf != 0)
        {
            m_audioEngine->attachReverbIr(buf);
            m_attachedIrPath = probe.irPath;
            m_slotWetGain    = 0.0f;  // dip; slew ramps it back.
        }
    }

    // Inside the nearest probe's reach → wet up to the accessibility ceiling;
    // beyond every probe → dry. The slew keeps both transitions click-free.
    const float target = inside ? m_audioEngine->reverbWetCap() : 0.0f;
    const float slew   = std::clamp(deltaTime * kReverbGainSlewPerSec, 0.0f, 1.0f);
    m_slotWetGain      = slewReverbWetGain(m_slotWetGain, target, slew);
    m_audioEngine->setReverbWetGain(m_slotWetGain);

    m_winningZoneName = inside ? ("probe #" + std::to_string(probe.id)) : std::string();

    // Per-source send: unity for any spatial source inside any probe's influence,
    // else 0 — the baked analogue of the zone send loop.
    scene.forEachEntity([&](Entity& entity)
    {
        auto* s = entity.getComponent<AudioSourceComponent>();
        if (s == nullptr)
        {
            return;
        }
        float send = 0.0f;
        if (s->spatial)
        {
            const glm::vec3 sourcePos = entity.getWorldPosition();
            for (const LoadedAcousticProbe& p : m_bakedProbes)
            {
                if (glm::length(p.position - sourcePos) <= p.influenceRadius)
                {
                    send = 1.0f;
                    break;
                }
            }
        }
        s->reverbSend = send;
    });

    return true;
}

std::vector<uint32_t> ReverbSystem::getOwnedComponentTypes() const
{
    // Reverb zones OR baked acoustic probes (NOT AudioSourceComponent): with the
    // default isForceActive() == false, the system activates when a scene has
    // either authored reverb zones or baked probes (B4) — a scene with sources
    // but neither stays dry. A baked-only scene (probes, no zones) must still
    // activate, or its baked IRs would never reach the slot.
    return {
        ComponentTypeId::get<ReverbZoneComponent>(),
        ComponentTypeId::get<AcousticProbeComponent>()
    };
}

} // namespace Vestige
