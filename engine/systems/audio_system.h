/// @file audio_system.h
/// @brief Domain system for audio playback and spatial sound.
#pragma once

#include "core/i_system.h"
#include "audio/audio_engine.h"

#include <string>

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

private:
    static inline const std::string m_name = "Audio";
    AudioEngine m_audioEngine;
    Engine* m_engine = nullptr;
};

} // namespace Vestige
