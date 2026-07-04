// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_reverb.h
/// @brief Phase 10 spatial audio — reverb-zone presets, per-zone
///        weight falloff, and param blending for smooth transitions.
///
/// Each zone is a sphere-with-falloff in world space: inside the
/// `radius` the zone is at full weight, between `radius` and
/// `radius + falloff` the weight drops linearly to zero, and past
/// that the zone contributes nothing. The engine-side ReverbSystem
/// chooses the single highest-weighted zone and the next-highest
/// neighbour, then blends their `ReverbParams` via
/// `blendReverbParams(a, b, t)` so the crossfade across a threshold
/// (doorway, cave mouth) is continuous.
///
/// The OpenAL EFX extension (`AL_EXT_EFX`, `ALC_EXT_EFX`) consumes
/// the blended params to drive `AL_EAXREVERB_*` properties on the
/// auxiliary effect slot. This module is headless and carries no
/// OpenAL linkage — the engine-side adapter does the `alEffectf`
/// calls.
///
/// Preset values are adapted from the Creative Labs EFX reverb
/// preset table (`efx-presets.h` — `EFX_REVERB_PRESET_GENERIC`,
/// `_ROOM`, `_HALL`, `_CAVE`, `_PLAIN`, `_UNDERWATER`) with the
/// subset of parameters that map cleanly to the standard reverb
/// model (non-EAX). Values are documented inline so future
/// adjustments stay grounded.
#pragma once

#include <string_view>
#include <vector>

namespace Vestige
{

/// @brief Canonical environment presets selectable per reverb zone.
///        Dense + stable for scene-file persistence.
enum class ReverbPreset
{
    Generic,      ///< Neutral mid-size room, safe default.
    SmallRoom,    ///< Bedroom / study / small chapel.
    LargeHall,    ///< Banquet hall / cathedral nave / auditorium.
    Cave,         ///< Rough stone cavern — long tail, high diffusion.
    Outdoor,      ///< Open plain — minimal early reflections.
    Underwater,   ///< Submerged — strong high-frequency damping.
};

/// @brief Reverb parameters that map 1:1 to the standard OpenAL
///        EFX reverb model (`AL_REVERB_*`). Values are dimensionless
///        where the spec uses a ratio; `decayTime` is in seconds.
struct ReverbParams
{
    float decayTime        = 1.49f;  ///< Seconds. EFX range [0.1, 20].
    float density          = 1.00f;  ///< [0, 1]. EFX modal density.
    float diffusion        = 1.00f;  ///< [0, 1]. Late echoes overlap.
    float gain             = 0.32f;  ///< [0, 1]. Overall reverb level.
    float gainHf           = 0.89f;  ///< [0, 1]. High-frequency gain.
    float reflectionsDelay = 0.007f; ///< Seconds. First-reflection delay.
    float lateReverbDelay  = 0.011f; ///< Seconds. Late-reverb delay.

    bool operator==(const ReverbParams& o) const
    {
        return decayTime        == o.decayTime
            && density          == o.density
            && diffusion        == o.diffusion
            && gain             == o.gain
            && gainHf           == o.gainHf
            && reflectionsDelay == o.reflectionsDelay
            && lateReverbDelay  == o.lateReverbDelay;
    }
    bool operator!=(const ReverbParams& o) const { return !(*this == o); }
};

/// @brief Returns the canonical parameters for a named preset.
///        Defaults to `Generic` for unknown values.
ReverbParams reverbPresetParams(ReverbPreset preset);

/// @brief Stable, human-readable label for a preset.
const char* reverbPresetLabel(ReverbPreset preset);

/// @brief Inverse of `reverbPresetLabel`: parse a preset from its label.
///        Unknown / empty labels fall back to `Generic`, so a scene file with
///        a stale or missing preset name deserialises to the safe default
///        rather than failing the load. Used by the scene (de)serializer (R3)
///        and the editor picker (R4).
ReverbPreset reverbPresetFromLabel(std::string_view label);

/// @brief Per-zone weight falloff.
///
/// Computes how much this zone contributes to the blended reverb at
/// a given listener position:
///
///   - distance ≤ radius               → weight = 1.0 (inside core).
///   - radius < distance ≤ radius+band → weight decays linearly to 0.
///   - distance > radius + band        → weight = 0 (outside).
///
/// @param zoneCenter  World position of the zone's core.
/// @param coreRadius  Radius of the full-weight core (>= 0).
/// @param falloffBand Linear-falloff band thickness outside the core (>= 0).
///                    Zero means a hard step from 1 to 0 at the radius.
/// @param distance    Euclidean distance from listener to zone center.
/// @returns Weight in [0, 1].
float computeReverbZoneWeight(float coreRadius,
                               float falloffBand,
                               float distance);

/// @brief Linear blend between two reverb parameter sets.
///
/// @param a Reverb params at `t == 0`.
/// @param b Reverb params at `t == 1`.
/// @param t Blend factor — clamped to [0, 1].
/// @returns Component-wise `a * (1 − t) + b * t` across every field.
ReverbParams blendReverbParams(const ReverbParams& a,
                                const ReverbParams& b,
                                float t);

/// @brief One reverb zone reduced to the scalar inputs the selection needs.
///
/// The engine-side `ReverbSystem` turns each `ReverbZoneComponent` into one of
/// these: `distance` is the listener→zone-centre distance (computed with glm on
/// the engine side), and the rest is the zone's authored shape + character. The
/// selection math itself stays glm-free so it is trivially unit-testable
/// (AX1's pure-function-then-thin-system split).
struct ReverbZoneEval
{
    float        distance    = 0.0f; ///< Listener → zone centre (m).
    float        coreRadius  = 5.0f; ///< Full-weight core radius (m).
    float        falloffBand = 2.0f; ///< Linear-falloff band thickness (m).
    ReverbParams params;             ///< Parametric character of this zone.
    float        wetGain     = 0.30f;///< Slot wet gain [0,1] when this zone wins.
};

/// @brief The winning zone + blended output for one frame's reverb slot.
struct ReverbSelection
{
    /// @brief Index (into the input vector) of the highest-weighted zone, or
    ///        -1 when the listener is inside no zone (→ dry).
    int   winner    = -1;

    /// @brief Index of the next-highest zone that also has weight > 0, or -1.
    int   neighbour = -1;

    /// @brief Blend factor toward the neighbour, `wNeighbour/(wWinner+wNeighbour)`
    ///        ∈ [0, 0.5]. 0 when there is no neighbour → pure winner character.
    float blendT    = 0.0f;

    /// @brief The winner's weight ∈ (0,1] — 1 deep in its core, → 0 at its edge.
    float winnerWeight = 0.0f;

    /// @brief `blendReverbParams(winner, neighbour, blendT)` — the character to
    ///        push to the parametric effect. Left at the `Generic` default when
    ///        `winner == -1`.
    ReverbParams blendedParams;

    /// @brief The slot wet gain to slew toward: the blended zone wet gain scaled
    ///        by `winnerWeight`, so it both blends between rooms *and* fades to 0
    ///        as the listener leaves every zone. 0 when `winner == -1`.
    float targetWetGain = 0.0f;
};

/// @brief Picks the winning reverb zone (+ neighbour) for the listener and
///        produces the blended params + target slot gain.
///
/// Weighs every zone via `computeReverbZoneWeight`; the highest weight wins and
/// the second-highest is the blend neighbour (zero-weight zones ignored). Ties
/// resolve to the lower index (deterministic). With no zone in range the result
/// is the dry default (`winner == -1`, `targetWetGain == 0`).
ReverbSelection selectReverbZone(const std::vector<ReverbZoneEval>& zones);

/// @brief Eases `current` toward `target` by `slewAmount` ∈ [0,1] (clamped) and
///        returns the new value (1 snaps). Mirrors `slewOcclusionFraction` — the
///        per-frame smoothing that turns entering/leaving a zone into a fade
///        rather than a step.
float slewReverbWetGain(float current, float target, float slewAmount);

} // namespace Vestige
