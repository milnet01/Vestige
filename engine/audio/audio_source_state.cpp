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
    AudioLodTier                lodTier,
    float                       loudnessMakeup,
    float                       reverbSend)
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
    // Occlusion stays in [0, 1]; AX9 loudness makeup can boost above 1.0, so
    // it is applied *after* the occlusion clamp and left for resolveSourceGain
    // to clamp the final composed gain (boosting a quiet clip up to full).
    const float volumeAfterOcclusion =
        std::max(0.0f, std::min(1.0f, comp.volume * occlusion)) * loudnessMakeup;

    state.gain = resolveSourceGain(
        mixer, comp.bus, volumeAfterOcclusion, duckingGain);
    // AX12: fold the editor solo gate into the spatial output gain (uploaded by
    // applySourceState). Binary {0,1}; resolveSourceGain itself is untouched so
    // the eviction/occlusion callers stay solo-agnostic.
    state.gain *= busSoloMultiplier(mixer, comp.bus);

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
    // AX2 R1 — carry the reverb send through. A source that has dropped to
    // 2D (no world position) or muted (silent) contributes nothing sensible
    // to the room reverb, so those tiers zero the send.
    state.reverbSend = std::max(0.0f, std::min(1.0f, reverbSend));

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
            state.reverbSend    = 0.0f;
            break;
        case AudioLodTier::Mute:
            state.gain       = 0.0f;
            state.reverbSend = 0.0f;
            break;
    }

    return state;
}

} // namespace Vestige
