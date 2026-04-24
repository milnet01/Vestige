// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_source_state.cpp
/// @brief Phase 10.9 Slice 2 P2 — pure compose implementation.
#include "audio/audio_source_state.h"

#include "audio/audio_occlusion.h"
#include "audio/audio_source_component.h"

#include <algorithm>

namespace Vestige
{

AudioSourceAlState composeAudioSourceAlState(
    const AudioSourceComponent& comp,
    const glm::vec3&            entityPosition,
    const AudioMixer&           mixer,
    float                       duckingGain)
{
    AudioSourceAlState state;

    state.position = entityPosition;
    state.velocity = comp.velocity;
    state.pitch    = comp.pitch;

    // Attenuation parameters go through untouched — the AL call site
    // pushes these as AL_REFERENCE_DISTANCE / AL_MAX_DISTANCE /
    // AL_ROLLOFF_FACTOR. The attenuationModel + spatial flag carry
    // through so the engine layer can pick the right
    // alDistanceModel / AL_SOURCE_RELATIVE configuration per source.
    state.referenceDistance = comp.minDistance;
    state.maxDistance       = comp.maxDistance;
    state.rolloffFactor     = comp.rolloffFactor;
    state.attenuationModel  = comp.attenuationModel;
    state.spatial           = comp.spatial;

    // Occlusion — derive the per-source attenuation multiplier from
    // the authored material + fraction. The resulting factor folds
    // into the `volume` input of resolveSourceGain so the existing
    // mixer × bus × duck × clamp pipeline applies uniformly; no new
    // clamp site is introduced.
    const AudioOcclusionMaterial material =
        occlusionMaterialFor(comp.occlusionMaterial);
    const float occlusion = computeObstructionGain(
        1.0f, material.transmissionCoefficient, comp.occlusionFraction);
    const float volumeAfterOcclusion =
        std::max(0.0f, std::min(1.0f, comp.volume * occlusion));

    state.gain = resolveSourceGain(
        mixer, comp.bus, volumeAfterOcclusion, duckingGain);

    return state;
}

} // namespace Vestige
