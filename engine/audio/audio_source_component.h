/// @file audio_source_component.h
/// @brief Entity component for 3D positioned audio sources.
#pragma once

#include "scene/component.h"

#include <string>

namespace Vestige
{

/// @brief Attaches a sound source to an entity for 3D spatial audio.
///
/// The AudioSystem manages the underlying OpenAL source lifecycle.
/// Components declare what sound to play and how; the system handles playback.
class AudioSourceComponent : public Component
{
public:
    AudioSourceComponent() = default;

    /// @brief Path to the audio file (WAV, MP3, FLAC, or OGG).
    std::string clipPath;

    /// @brief Volume (0.0 = silent, 1.0 = full volume).
    float volume = 1.0f;

    /// @brief Pitch multiplier (1.0 = normal).
    float pitch = 1.0f;

    /// @brief Distance at which attenuation begins (meters).
    float minDistance = 1.0f;

    /// @brief Distance at which sound is inaudible (meters).
    float maxDistance = 50.0f;

    /// @brief Whether the sound loops.
    bool loop = false;

    /// @brief Whether the sound plays automatically on scene load.
    bool autoPlay = false;

    /// @brief Whether the sound is spatially positioned (3D). False = 2D/ambient.
    bool spatial = true;

    /// @brief Creates a deep copy of this component.
    std::unique_ptr<Component> clone() const override;
};

} // namespace Vestige
