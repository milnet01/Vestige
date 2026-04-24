// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_system.cpp
/// @brief AudioSystem implementation.
#include "systems/audio_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "audio/audio_source_component.h"
#include "scene/component.h"

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
    m_audioEngine.setMixerSnapshot(m_engine->getAudioMixer());
    m_audioEngine.updateGains();
}

std::vector<uint32_t> AudioSystem::getOwnedComponentTypes() const
{
    return {
        ComponentTypeId::get<AudioSourceComponent>()
    };
}

} // namespace Vestige
