// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_lod.h
/// @brief Phase 10 audio quick-wins (AX5) — per-source audio level-of-detail
///        ladder.
///
/// Dense-source scenes (markets, crowds, battle) must stay at 60 FPS.
/// Distant / heavily-occluded low-priority sources shouldn't keep paying
/// for full spatialisation. A pure decision function picks a tier per
/// source per frame; the compose stage applies it.
///
/// HRTF is a **device-global** ALC attribute — there is no per-source
/// HRTF toggle — so a tier cannot "turn HRTF off for one source". Tiers
/// act only on the controllable per-source work: the EFX low-pass
/// (occlusion + air absorption, the AX6 path) and 3D-vs-2D positioning.
#pragma once

namespace Vestige
{

enum class SoundPriority;  // audio_mixer.h

/// @brief Per-source level-of-detail tiers, richest → cheapest.
enum class AudioLodTier
{
    Full,          ///< 3D positioned, full attenuation + EFX low-pass.
    CheapSpatial,  ///< 3D + distance gain, but skip the per-source low-pass.
    Drop2D,        ///< Collapse to 2D (head-relative) at attenuated gain.
    Mute,          ///< Gain 0 (kept alive for cheap re-promotion).
};

/// @brief Tuning for the ladder. Factors are fractions of a source's
///        `maxDistance`; `hysteresis` is the dead-band (also a fraction
///        of `maxDistance`) that stops a source flapping between tiers
///        when it hovers on a boundary.
struct AudioLodConfig
{
    float cheapDistanceFactor = 0.6f;   ///< ≥ this ratio → drop the per-source low-pass.
    float drop2DFactor        = 0.85f;  ///< ≥ this ratio → collapse to 2D.
    float muteFactor          = 1.0f;   ///< ≥ this ratio → silent.
    float hysteresis          = 0.05f;  ///< dead-band fraction resisting tier change.
    bool  enabled             = true;
};

/// @brief Picks the LOD tier for one source this frame.
///
/// The decision is driven by an **effective audibility-loss ratio** =
/// `max(distance / maxDistance, occlusionFraction)` — a near source that
/// is heavily occluded is as treble-dead as a distant one, so it can be
/// cheapened too (no new magic threshold: occlusion reuses the same
/// distance-factor boundaries). Boundaries are applied with a
/// `previousTier`-seeded hysteresis dead-band. `SoundPriority::Critical`
/// (dialogue, scripted, boss stingers) never drops below `CheapSpatial`,
/// so accessibility-critical audio keeps its 3D position.
///
/// Pure and pool-agnostic by design: whether a `Mute` source is kept
/// alive or released under pool pressure is decided in the apply layer
/// (which knows live pool occupancy), not here (§12-Q2).
AudioLodTier audioLodTier(float distance, float maxDistance,
                          float occlusionFraction, SoundPriority priority,
                          AudioLodTier previousTier, const AudioLodConfig& cfg);

/// @brief Stable label for debug panels / tests.
const char* audioLodTierLabel(AudioLodTier tier);

} // namespace Vestige
