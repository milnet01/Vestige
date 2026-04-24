// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_system.h
/// @brief Domain system for audio playback and spatial sound.
#pragma once

#include "core/i_system.h"
#include "audio/audio_engine.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Manages audio playback, spatial sound, and listener positioning.
///
/// Owns the AudioEngine subsystem that wraps OpenAL for 3D audio. Syncs
/// the listener to the camera each frame. Fails gracefully if no audio
/// hardware is available — the engine continues without sound.
class AudioSystem : public ISystem
{
public:
    AudioSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

    // -- Accessors --
    AudioEngine& getAudioEngine() { return m_audioEngine; }
    const AudioEngine& getAudioEngine() const { return m_audioEngine; }

    /// @brief Whether audio hardware is available.
    bool isAvailable() const { return m_audioEngine.isAvailable(); }

    /// @brief Phase 10.9 P2 — read-only access to the per-entity
    ///        active-source map. Tests use this to verify the
    ///        auto-play + source-tracking pipeline without touching AL.
    const std::unordered_map<std::uint32_t, unsigned int>&
    activeSources() const { return m_activeSources; }

private:
    static inline const std::string m_name = "Audio";
    AudioEngine m_audioEngine;
    Engine* m_engine = nullptr;

    /// @brief Phase 10.9 P2 — maps entity ID → OpenAL source ID for
    ///        every `AudioSourceComponent` that has been acquired by
    ///        this system (auto-play or explicit trigger). Cleared
    ///        when the component is removed or the source stops
    ///        (reaped in the per-frame update).
    std::unordered_map<std::uint32_t, unsigned int> m_activeSources;
};

} // namespace Vestige
