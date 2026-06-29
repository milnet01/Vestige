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
    float                       duckingGain,
    const glm::vec3&            listenerPosition,
    const AirAbsorptionParams&  air,
    AudioLodTier                lodTier)
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

    // AX6 — per-source high-frequency damping. Two independent terms,
    // combined multiplicatively in the linear HF-gain domain:
    //   - occlusionLowPassHf: the HF complement of the (previously
    //     dead) material low-pass — 1.0 means "no muffling", lower
    //     means heavier. This wires up `computeObstructionLowPass`,
    //     which was computed but never sent to OpenAL pre-AX6.
    //   - airAbsorptionHfGain: ISO 9613-1 distance/temperature/humidity
    //     rolloff, only meaningful for spatial sources.
    const float occlusionDamp = computeObstructionLowPass(
        material.lowPassAmount, comp.occlusionFraction);
    const float occlusionLowPassHf = 1.0f - occlusionDamp;  // [0,1], 1 = clear

    float airHfGain = 1.0f;
    if (comp.spatial)
    {
        const float distance = glm::length(entityPosition - listenerPosition);
        airHfGain = airAbsorptionHfGain(distance, air);
    }

    state.lowPassGainHf = std::max(
        0.0f, std::min(1.0f, occlusionLowPassHf * airHfGain));

    // AX5 — apply the level-of-detail tier the caller picked. Demoted
    // tiers shed the controllable per-source work: the EFX low-pass and
    // 3D positioning. (HRTF is device-global and cannot be toggled per
    // source, so no tier touches it.)
    state.lodTier = lodTier;
    switch (lodTier)
    {
        case AudioLodTier::Full:
            break;  // full work — nothing to shed
        case AudioLodTier::CheapSpatial:
            state.lowPassGainHf = 1.0f;  // skip the per-source low-pass
            break;
        case AudioLodTier::Drop2D:
            state.spatial       = false;  // collapse to head-relative 2D
            state.lowPassGainHf = 1.0f;
            break;
        case AudioLodTier::Mute:
            state.gain = 0.0f;
            break;
    }

    return state;
}

} // namespace Vestige
