// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_occlusion.h
/// @brief Phase 10 spatial audio — material-based occlusion and
///        obstruction gain / low-pass model.
///
/// When a physics raycast between a source and the listener hits
/// geometry, the engine consults this module to decide how much
/// gain is lost and how much high-frequency damping to apply. Two
/// independent axes:
///
///   - **Occlusion** — full sight-line blockage. The ray cannot
///     reach the listener without passing through a solid surface.
///     Gain is multiplied by the material's transmission
///     coefficient and a heavy low-pass filter is applied.
///
///   - **Obstruction** — partial blockage. A secondary ray or a
///     sphere-cast detects the edge of an obstacle between source
///     and listener (doorframe, low wall). The `fractionBlocked`
///     in [0, 1] blends linearly between the unobstructed path and
///     a fully occluded one.
///
/// Diffraction (sound wrapping around corners) is *not* modelled in
/// this module — it is the engine-side raycaster's responsibility
/// to pick a secondary source position that hugs the diffraction
/// edge and feed that into the normal attenuation path. Keeping the
/// pure-function layer blind to geometry preserves testability.
///
/// Reference values come from standard architectural-acoustics
/// tables (transmission loss, NRC, frequency-weighted absorption).
/// Exact numeric constants are judgement calls calibrated to what
/// sounds right in a first-person walkthrough rather than strict
/// dB-measured values — the point is relative ordering (Concrete
/// muffles far more than Wood) not laboratory accuracy.
#pragma once

namespace Vestige
{

/// @brief Canonical materials the raycaster can tag a hit with.
///        The enum is dense and stable so it can be persisted as an
///        integer in scene files without version gymnastics.
enum class AudioOcclusionMaterialPreset
{
    Air,        ///< No material — pass-through (T=1, LP=0).
    Cloth,      ///< Curtain, tapestry, light drape.
    Wood,       ///< Doors, thin walls, furniture.
    Glass,      ///< Windows — transmits sound but less muffling than wood.
    Stone,      ///< Brick / masonry interior walls.
    Concrete,   ///< Thick exterior walls, bunkers, basements.
    Metal,      ///< Sheet metal, vehicle armour — strong reflector.
    Water,      ///< Submerged surfaces — heavy high-frequency damping.
};

/// @brief Per-material transmission and filtering parameters.
///
/// `transmissionCoefficient` scales gain directly: 1.0 means the
/// material is acoustically transparent, 0.0 means no sound passes
/// through. `lowPassAmount` controls high-frequency damping on the
/// transmitted signal: 0.0 means the spectrum is preserved, 1.0
/// means only low-end content survives (heavy muffling).
struct AudioOcclusionMaterial
{
    float transmissionCoefficient = 1.0f;
    float lowPassAmount           = 0.0f;
};

/// @brief Returns the canonical parameters for a named material.
///
/// The numeric values are calibrated for first-person walkthrough
/// ambience — they are deliberately exaggerated compared to
/// laboratory transmission-loss measurements so differences between
/// materials remain audible without extreme source levels.
AudioOcclusionMaterial occlusionMaterialFor(AudioOcclusionMaterialPreset preset);

/// @brief Stable, human-readable label for a material preset —
///        used by debug panels, the editor material picker, and
///        scene serialisation diagnostics.
const char* occlusionMaterialLabel(AudioOcclusionMaterialPreset preset);

/// @brief Blends between an unoccluded path and one that travels
///        through a material, based on the fraction of the line
///        between source and listener that passes through the
///        obstacle.
///
/// @param openGain        The gain at this source with no obstacles.
/// @param transmissionCoefficient
///                        Material transmission (`AudioOcclusionMaterial`).
/// @param fractionBlocked In [0, 1]. 0 = line-of-sight is clear;
///                        1 = line-of-sight passes entirely through
///                        the material. Values outside the range
///                        are clamped.
/// @returns A gain value in [0, `openGain`].
float computeObstructionGain(float openGain,
                              float transmissionCoefficient,
                              float fractionBlocked);

/// @brief Blends between no damping and full material damping based
///        on `fractionBlocked`. Output is in [0, `lowPassAmount`]
///        and feeds the EFX low-pass filter cutoff calculation when
///        the engine-side EFX slot is connected.
float computeObstructionLowPass(float lowPassAmount,
                                 float fractionBlocked);

} // namespace Vestige
