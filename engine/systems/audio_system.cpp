// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_system.cpp
/// @brief AudioSystem implementation.
#include "systems/audio_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "audio/audio_source_component.h"
#include "audio/audio_source_state.h"
#include "scene/component.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

namespace Vestige
{

bool AudioSystem::initialize(Engine& engine)
{
    m_engine = &engine;

    if (!m_audioEngine.initialize())
    {
        Logger::warning("[AudioSystem] Audio engine initialization failed "
                        "— audio will be unavailable");
        // Non-fatal: engine continues without audio
    }

    Logger::info("[AudioSystem] Initialized");
    return true;
}

void AudioSystem::shutdown()
{
    m_audioEngine.shutdown();
    m_engine = nullptr;
    Logger::info("[AudioSystem] Shut down");
}

void AudioSystem::update(float deltaTime)
{
    if (!m_audioEngine.isAvailable() || !m_engine)
    {
        return;
    }

    // Sync listener to camera position
    Camera& camera = m_engine->getCamera();
    m_audioEngine.updateListener(
        camera.getPosition(),
        camera.getFront(),
        glm::vec3(0.0f, 1.0f, 0.0f));  // World up

    // Phase 10.9 P3 — advance the engine-owned DuckingState by the
    // frame delta and publish the resulting currentGain to the audio
    // engine so updateGains folds it into every AL_GAIN push. The
    // state lives on Engine (authoritative single source of truth);
    // editor / gameplay code mutates `.triggered` via
    // `engine.getDuckingState()`.
    updateDucking(m_engine->getDuckingState(),
                  m_engine->getDuckingParams(),
                  deltaTime);
    m_audioEngine.setDuckingSnapshot(
        m_engine->getDuckingState().currentGain);

    // Phase 10.7 slice A2 — publish the latest mixer snapshot and
    // re-compose AL_GAIN for every live source so mid-play slider
    // moves are heard on the next frame rather than only on newly
    // acquired sources. Also reaps stopped sources from the
    // playback registry.
    // Phase 10.9 W7: pass the engine-owned mixer by pointer rather than
    // copying the whole struct each frame. Lifetime is fine — the Engine
    // outlives the AudioEngine in the destruction order.
    m_audioEngine.setMixerSnapshot(&m_engine->getAudioMixer());
    m_audioEngine.updateGains();

    // Phase 10.9 P2 — iterate every AudioSourceComponent in the active
    // scene, auto-acquire an AL source for any component tagged
    // `autoPlay=true` that we aren't already tracking, and push the
    // composed AudioSourceAlState so pitch / velocity / occlusion /
    // attenuation edits reach AL every frame instead of only at
    // source-acquire time. Nine component fields that were dead data
    // in shipping code (pitch, velocity, attenuationModel, min/max/
    // rolloff, occlusionMaterial/Fraction, spatial, autoPlay) now
    // drive the playing source live.
    Scene* scene = m_engine->getSceneManager().getActiveScene();
    if (scene == nullptr)
    {
        return;
    }

    const AudioMixer& mixer = m_engine->getAudioMixer();
    const float duck        = m_engine->getDuckingState().currentGain;

    scene->forEachEntity([this, &mixer, duck](Entity& entity)
    {
        auto* comp = entity.getComponent<AudioSourceComponent>();
        if (comp == nullptr)
        {
            return;
        }

        const std::uint32_t entityId = entity.getId();
        const glm::vec3 position = entity.getWorldPosition();

        auto it = m_activeSources.find(entityId);
        if (it == m_activeSources.end())
        {
            // Not yet playing. Auto-acquire when authored. Explicit
            // triggers (script graphs, gameplay code) call playSound*
            // directly — those stay untracked-by-entity because the
            // caller owns the lifetime decision.
            if (!comp->autoPlay || comp->clipPath.empty())
            {
                return;
            }
            unsigned int source = 0;
            if (comp->spatial)
            {
                AttenuationParams params;
                params.referenceDistance = comp->minDistance;
                params.maxDistance       = comp->maxDistance;
                params.rolloffFactor     = comp->rolloffFactor;
                source = m_audioEngine.playSoundSpatial(
                    comp->clipPath, position, comp->velocity, params,
                    comp->volume, comp->loop, comp->bus, comp->priority);
            }
            else
            {
                source = m_audioEngine.playSound2D(
                    comp->clipPath, comp->volume, comp->bus, comp->priority);
            }
            // Record even a 0 source ID — it marks "we've attempted
            // autoplay for this entity so don't retry every frame".
            // A 0 entry is purged in the reap pass below along with
            // entities whose sources have stopped, which lets the
            // entity pick up again if the component changes
            // (e.g. autoPlay re-toggled).
            m_activeSources[entityId] = source;
            if (source != 0)
            {
                // Push the full composed state immediately so pitch
                // / occlusion overrides on the component are heard on
                // frame 1 rather than frame 2.
                const AudioSourceAlState state =
                    composeAudioSourceAlState(*comp, position, mixer, duck);
                m_audioEngine.applySourceState(source, state);
            }
            return;
        }

        // Tracked — push the per-frame state.
        const unsigned int source = it->second;
        if (source == 0)
        {
            // Autoplay attempted but no source acquired (pool
            // exhausted, file missing, or no hardware). Skip this
            // frame; the reap pass below will prune the entry so
            // a future tick can retry.
            return;
        }
        const AudioSourceAlState alState =
            composeAudioSourceAlState(*comp, position, mixer, duck);
        m_audioEngine.applySourceState(source, alState);
    });

    // Reap entries whose source has stopped or whose entity has
    // disappeared. Snapshot-safe iteration: the scene traversal
    // above doesn't mutate the map, so a simple forward walk
    // suffices.
    for (auto it = m_activeSources.begin(); it != m_activeSources.end(); )
    {
        const bool entityGone = scene->findEntityById(it->first) == nullptr;
        const bool sourceDead =
            it->second == 0 ||
            !m_audioEngine.isSourcePlaying(it->second);
        if (entityGone || sourceDead)
        {
            it = m_activeSources.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::vector<uint32_t> AudioSystem::getOwnedComponentTypes() const
{
    return {
        ComponentTypeId::get<AudioSourceComponent>()
    };
}

} // namespace Vestige
