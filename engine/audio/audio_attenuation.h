// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_attenuation.h
/// @brief Phase 10 spatial audio — distance-attenuation curves for 3D
///        positioned sound sources.
///
/// Three canonical curves, each matching one of OpenAL's documented
/// distance models, plus a pass-through `None` for unattenuated
/// ambient / music / UI sounds:
///
///   - Linear            →  AL_LINEAR_DISTANCE_CLAMPED
///                         gain = clamp01((maxDist - d)
///                                         / (maxDist - refDist))
///   - Inverse distance  →  AL_INVERSE_DISTANCE_CLAMPED
///                         gain = refDist /
///                                (refDist + rolloff * (d - refDist))
///   - Exponential       →  AL_EXPONENT_DISTANCE_CLAMPED
///                         gain = (d / refDist) ^ (-rolloff)
///
/// The module is pure-function: the engine consults
/// `computeAttenuation` for CPU-side gain calculations (priority
/// sorting, virtual-voice culling, the editor preview) and
/// `alDistanceModelFor(model)` to tell OpenAL which built-in curve
/// to evaluate for native playback. OpenAL's own math and this
/// module's math must agree at source positions — enforced by the
/// tests in `test_audio_attenuation.cpp`.
///
/// Reference: OpenAL 1.1 Specification §3.4 ("Attenuation by
/// Distance") and OpenAL Soft Programmer's Guide §5.3. The canonical
/// formulas are textbook; they have no coefficients to fit, so the
/// engine-wide Formula Workbench rule (author via fit + export)
/// doesn't apply here — see CLAUDE.md Rule 11 note.
#pragma once

namespace Vestige
{

/// @brief Distance-attenuation model selector.
///
/// Stored per-source (`AudioSourceComponent::attenuationModel`) and
/// applied to the corresponding OpenAL source. The engine-wide
/// distance model (`AudioEngine::setDistanceModel`) must match for
/// native OpenAL playback to follow the curve; for CPU-side sorting
/// / virtual-voice decisions the `computeAttenuation` function
/// reproduces the same math.
enum class AttenuationModel
{
    None,             ///< No attenuation — constant gain. Use for 2D / ambient / music.
    Linear,           ///< Linear ramp to silence at maxDistance.
    InverseDistance,  ///< `refDist / (refDist + rolloff * (d - refDist))`.
    Exponential,      ///< `(d / refDist) ^ (-rolloff)`.
};

/// @brief Per-source attenuation parameters.
///
/// Units are engine meters (matching the physics system). Defaults
/// are the OpenAL 1.1 specification defaults.
struct AttenuationParams
{
    /// Distance (m) at which the source is at full authored gain.
    /// Below this distance the gain is clamped to the full value.
    float referenceDistance = 1.0f;

    /// Distance (m) beyond which attenuation stops. The `*_CLAMPED`
    /// OpenAL models hold the gain constant past this distance.
    /// Must be > `referenceDistance`.
    float maxDistance       = 50.0f;

    /// Rolloff steepness multiplier. 1.0 matches the canonical
    /// form of the selected model; 2.0 is roughly inverse-square
    /// under `InverseDistance`; 0.0 flattens the curve.
    float rolloffFactor     = 1.0f;
};

/// @brief Computes the gain multiplier for `distance` under `model`.
/// @returns A value in [0, 1]. 1.0 when `distance <= referenceDistance`
///          (or model is `None`); 0.0 past `maxDistance` for `Linear`.
///          `InverseDistance` and `Exponential` asymptote to 0 but
///          return the gain evaluated at `maxDistance` beyond that
///          (matching OpenAL's `_CLAMPED` variants).
float computeAttenuation(AttenuationModel model,
                         const AttenuationParams& params,
                         float distance);

/// @brief Returns a stable, human-readable label for a model — used
///        by tests, debug panels, and the editor's source inspector.
const char* attenuationModelLabel(AttenuationModel model);

/// @brief Maps the engine-level `AttenuationModel` to the matching
///        OpenAL distance-model constant (`AL_*_DISTANCE_CLAMPED` or
///        `AL_NONE`). The constant is returned as an `int` so this
///        header does not pull in `<AL/al.h>` — callers cast when
///        invoking `alDistanceModel`.
int alDistanceModelFor(AttenuationModel model);

} // namespace Vestige
