// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_source_component.h
/// @brief Entity component for 3D positioned audio sources.
#pragma once

#include "audio/audio_attenuation.h"
#include "scene/component.h"

#include <glm/glm.hpp>

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

    /// @brief Distance at which attenuation begins (meters). Maps to
    ///        `AttenuationParams::referenceDistance` for OpenAL.
    float minDistance = 1.0f;

    /// @brief Distance at which sound is inaudible (meters). Maps to
    ///        `AttenuationParams::maxDistance` for OpenAL.
    float maxDistance = 50.0f;

    /// @brief Steepness of the attenuation curve. 1.0 matches the
    ///        canonical form of `attenuationModel`; 2.0 produces a
    ///        sharper falloff; 0.0 flattens the curve entirely.
    float rolloffFactor = 1.0f;

    /// @brief Distance-attenuation curve this source follows.
    ///
    /// Defaults to `InverseDistance` to match the engine-wide Phase
    /// 9C behaviour, so existing scenes sound identical until they
    /// explicitly opt into a different curve.
    AttenuationModel attenuationModel = AttenuationModel::InverseDistance;

    /// @brief Velocity in engine meters per second for Doppler shift.
    ///
    /// Zero (the default) disables the per-source Doppler contribution
    /// — stationary torches, UI blips, and ambient loops can ignore
    /// this field. Moving emitters (projectiles, vehicles, patrolling
    /// NPCs) should update this alongside their transform each frame;
    /// the AudioSystem uploads it to `AL_VELOCITY`.
    glm::vec3 velocity = glm::vec3(0.0f);

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
