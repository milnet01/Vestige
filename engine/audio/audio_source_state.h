// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_source_state.h
/// @brief Phase 10.9 Slice 2 P2 — pure-function compose of the per-frame
///        OpenAL state for one `AudioSourceComponent`.
///
/// Every field on `AudioSourceComponent` (pitch, velocity,
/// attenuation curve + reference / max / rolloff, material-based
/// occlusion, bus routing, spatial flag) is fed through a pure
/// helper that produces an `AudioSourceAlState` struct. The engine
/// side (`AudioEngine::applySourceState`) takes the struct and
/// issues the `alSource*` calls; tests call the helper directly and
/// read the struct fields. This is the only way to verify the
/// compose math without a real OpenAL device.
#pragma once

#include "audio/audio_attenuation.h"
#include "audio/audio_mixer.h"

#include <glm/glm.hpp>

namespace Vestige
{

class AudioSourceComponent;

/// @brief All per-frame state pushed into OpenAL for a single source.
///
/// The struct mirrors the set of `alSource*f` calls the engine makes
/// each frame so unit tests can assert against the composed result
/// rather than trying to observe AL side-effects.
struct AudioSourceAlState
{
    glm::vec3 position      = glm::vec3(0.0f); ///< AL_POSITION (world).
    glm::vec3 velocity      = glm::vec3(0.0f); ///< AL_VELOCITY (m/s).
    float     pitch         = 1.0f;            ///< AL_PITCH.
    float     gain          = 1.0f;            ///< AL_GAIN (final composed).

    /// @name Attenuation
    /// @{
    float            referenceDistance = 1.0f;  ///< AL_REFERENCE_DISTANCE.
    float            maxDistance       = 50.0f; ///< AL_MAX_DISTANCE.
    float            rolloffFactor     = 1.0f;  ///< AL_ROLLOFF_FACTOR.
    AttenuationModel attenuationModel  = AttenuationModel::InverseDistance;
    /// @}

    /// @brief True if the source is 3D-positioned; false for 2D/UI.
    bool spatial = true;
};

/// @brief Pure compose of the per-frame `AudioSourceAlState` from a
///        component + world-space entity position + engine mixer +
///        ducking snapshot.
///
/// Composition (matches `AudioEngine::updateGains` for fire-and-forget
/// sources and extends it with occlusion, pitch, velocity, and the
/// per-source attenuation curve):
///
/// ```
/// gain = resolveSourceGain(
///            mixer, comp.bus,
///            comp.volume * occlusionGain(comp, 1.0f),
///            duckingGain);
/// ```
///
/// `occlusionGain` is derived from `comp.occlusionMaterial` +
/// `comp.occlusionFraction` via `computeObstructionGain` with
/// `openGain = 1.0` — the multiplier feeds into the `volume` input
/// to `resolveSourceGain` so the existing mixer / bus / duck / clamp
/// pipeline applies uniformly without a new clamp site.
///
/// All other fields are straight copies from the component with no
/// per-frame transformation — the AL call site pushes them as-is.
AudioSourceAlState composeAudioSourceAlState(
    const AudioSourceComponent& comp,
    const glm::vec3&            entityPosition,
    const AudioMixer&           mixer,
    float                       duckingGain);

} // namespace Vestige
