// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file reverb_system.cpp
/// @brief ReverbSystem implementation (AX2 R3 — zone selection + slot drive).
#include "systems/reverb_system.h"

#include "audio/audio_engine.h"
#include "audio/audio_reverb.h"
#include "audio/audio_source_component.h"
#include "audio/reverb_zone_component.h"
#include "core/engine.h"
#include "core/system_registry.h"
#include "systems/audio_system.h"
#include "scene/component.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <glm/glm.hpp>

#include <algorithm>
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
    };
    std::vector<GatheredZone> zones;
    scene->forEachEntity([&](Entity& entity)
    {
        auto* z = entity.getComponent<ReverbZoneComponent>();
        if (z == nullptr)
        {
            return;
        }
        zones.push_back({ entity.getWorldPosition(), z->coreRadius, z->falloffBand,
                          reverbPresetParams(z->preset), z->wetGain, &z->irPath });
    });

    // --- Pick the winning zone (+ neighbour) from the listener position.
    std::vector<ReverbZoneEval> evals;
    evals.reserve(zones.size());
    for (const GatheredZone& z : zones)
    {
        evals.push_back({ glm::length(z.center - listenerPos),
                          z.coreRadius, z.falloffBand, z.params, z.wetGain });
    }
    const ReverbSelection sel = selectReverbZone(evals);

    // --- Drive the slot: character (params or IR swap) + slewed wet gain.
    const float slew = std::clamp(deltaTime * kReverbGainSlewPerSec, 0.0f, 1.0f);
    m_slotWetGain = slewReverbWetGain(m_slotWetGain, sel.targetWetGain, slew);

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

std::vector<uint32_t> ReverbSystem::getOwnedComponentTypes() const
{
    // ReverbZoneComponent (NOT AudioSourceComponent): with the default
    // isForceActive() == false, the system activates only when a scene has
    // reverb zones — a scene with sources but no zone stays dry.
    return {
        ComponentTypeId::get<ReverbZoneComponent>()
    };
}

} // namespace Vestige
