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

#include "audio/audio_air_absorption.h"
#include "audio/audio_attenuation.h"
#include "audio/audio_lod.h"
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

    /// @brief AX6 — linear high-frequency gain in [0, 1] pushed into the
    ///        EFX low-pass filter (`AL_LOWPASS_GAINHF`). 1.0 = no HF
    ///        damping (the pre-AX6 behaviour). Combines occlusion's
    ///        material low-pass with the distance-driven air-absorption
    ///        term, multiplicatively. The engine layer only applies the
    ///        filter for spatial sources and only when EFX is available
    ///        (silent no-op otherwise — degrades to gain-only).
    float lowPassGainHf = 1.0f;

    /// @brief AX5 — the level-of-detail tier this source was composed at.
    ///        `Full` (default) is the pre-AX5 behaviour. Carried on the
    ///        state for the apply layer (the `Mute` keep-alive/release
    ///        decision) and debug panels.
    AudioLodTier lodTier = AudioLodTier::Full;

    /// @brief AX2 R1 — reverb send level in [0, 1]. When > 0 and the
    ///        engine has a live reverb aux-effect slot, the apply layer
    ///        routes this source through `AL_AUXILIARY_SEND_FILTER` so it
    ///        contributes to (and is coloured by) the room reverb. 0 (the
    ///        default) leaves the source fully dry — the pre-AX2 behaviour.
    ///        v1 policy: a source inside a reverb zone sends at unity and
    ///        the per-room wet level is the slot gain; per-source send
    ///        scaling is a documented follow-up.
    float reverbSend = 0.0f;
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
///
/// AX6 extends the seam with two trailing parameters:
///   - @a listenerPosition — the camera/listener world position, needed
///     to compute the listener↔source distance the air-absorption curve
///     reads. Defaults to the origin so the dozen pre-AX6 call sites that
///     omit it keep compiling (distance from origin; the air term affects
///     only `lowPassGainHf`, which those tests do not assert).
///   - @a air — the per-frame weather snapshot. `air.enabled == false`
///     (or a 2D/non-spatial source) leaves the air term at 1.0.
///
/// `lowPassGainHf = occlusionLowPassHf × airAbsorptionHfGain`, both in
/// the linear HF-gain domain. The occlusion term is the (newly-wired)
/// HF complement of `computeObstructionLowPass`; the air term is gated
/// on `comp.spatial` (distance is meaningless for 2D sources).
///
/// AX5 adds a trailing, defaulted `lodTier`. The pure tier decision is
/// made by the caller (`AudioSystem`, which owns the per-entity
/// previous-tier state); this function just *applies* it:
///   - `Full` — no change (the default; pre-AX5 behaviour).
///   - `CheapSpatial` — skip the per-source low-pass (`lowPassGainHf` →
///     1.0); keep 3D + distance gain.
///   - `Drop2D` — collapse to 2D (`spatial` → false) at the attenuated
///     gain; no low-pass.
///   - `Mute` — gain → 0 (the source is kept alive by the apply layer).
///
/// AX9 adds a trailing, defaulted `loudnessMakeup` — the per-clip linear
/// loudness-normalisation gain (`AudioEngine::loudnessMakeupForPath`). It
/// folds into the `volume` input alongside occlusion, so the same mixer /
/// bus / duck / clamp pipeline applies; `1.0` (the default) is a no-op,
/// keeping the pre-AX9 call sites unchanged.
///
/// AX2 R1 adds a trailing, defaulted `reverbSend` — the per-source reverb
/// aux-send level in [0, 1], copied straight onto the composed state for the
/// apply layer's `AL_AUXILIARY_SEND_FILTER` routing. `0.0` (the default)
/// leaves the source dry, keeping the pre-AX2 call sites unchanged.
AudioSourceAlState composeAudioSourceAlState(
    const AudioSourceComponent& comp,
    const glm::vec3&            entityPosition,
    const AudioMixer&           mixer,
    float                       duckingGain,
    const glm::vec3&            listenerPosition = glm::vec3(0.0f),
    const AirAbsorptionParams&  air              = AirAbsorptionParams{},
    AudioLodTier                lodTier          = AudioLodTier::Full,
    float                       loudnessMakeup   = 1.0f,
    float                       reverbSend       = 0.0f);

} // namespace Vestige
