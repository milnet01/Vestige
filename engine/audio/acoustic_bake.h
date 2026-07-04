// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file acoustic_bake.h
/// @brief AX3 B2 — offline image-source impulse-response bake core (pure).
#pragma once

#include "physics/surface_material.h"

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief A finite planar reflecting surface, the unit the image-source baker
///        reflects across.
///
/// The `plane` is the infinite plane the image-source method mirrors probes
/// across (`n·x + d = 0`, `n` unit-length); `area` is the facet's finite
/// surface area `Sᵢ` (m²), the quantity Sabine's equation sums; `material` keys
/// the mid-frequency absorption α (`surfaceMaterialAbsorption`). Facet
/// extraction from the static physics geometry is the bake driver's job (B3);
/// this struct is all the pure core needs.
struct ReflectingFacet
{
    glm::vec4 plane = glm::vec4(0.0f);              ///< (nx,ny,nz,d), |n| = 1.
    float area = 0.0f;                              ///< Finite surface area Sᵢ (m²).
    SurfaceMaterial material = SurfaceMaterial::Default;
};

/// @brief Mid-frequency (≈500 Hz–1 kHz) Sabine absorption coefficient α for a
///        surface material. Literature-seeded architectural-acoustics values
///        (design §6.2); a Formula Workbench refinement candidate (§10).
///        Distinct from AX1's occlusion transmission coefficients — reflection
///        α is separate data keyed off the same `SurfaceMaterial` identity.
float surfaceMaterialAbsorption(SurfaceMaterial material);

/// @brief Offline bake parameters (design defaults). Not per-room geometry —
///        that flows in through `bakeProbeIr`'s facet list + volume.
struct BakeParams
{
    int reflectionOrder = 2;              ///< Image-source order K (clamped [0,8]).
    float speedOfSound = 343.0f;          ///< m/s (dry air, 20 °C).
    int sampleRate = 48000;               ///< IR sample rate (Hz).
    float maxIrSeconds = 6.0f;            ///< Hard cap on IR length.
    float minIrSeconds = 0.05f;           ///< Floor so an open room still yields a short IR.
    unsigned int tailSeed = 0x9E3779B9u;  ///< Diffuse-tail noise seed base (mixed with probe pos → deterministic).
};

/// @brief Sabine reverberation time RT60 (s): `0.161·V / Σ(Sᵢ·αᵢ)`. Returns 0
///        for degenerate/open geometry (no absorbing surface, or zero volume) —
///        the caller then bakes early reflections only, no statistical tail.
float sabineRt60(float roomVolumeM3, const std::vector<ReflectingFacet>& facets);

/// @brief Schroeder backward-integration RT60 estimate of an impulse response
///        (T30: linear fit of the energy-decay curve over −5…−35 dB,
///        extrapolated to −60 dB). Validation/verification helper — measures
///        the decay an IR actually exhibits. Returns 0 when the IR is too short
///        or silent to fit.
float estimateRt60(const std::vector<float>& ir, int sampleRate);

/// @brief Bake a mono impulse response at `probePos` for a room described by
///        `facets` (+ its AABB volume `roomVolumeM3`, computed at facet
///        extraction — B3). Pure: no engine/physics access, so it runs across
///        MT2 workers over probes.
///
/// Early reflections use the image-source method (Allen & Berkley 1979) up to
/// `params.reflectionOrder`: each image contributes an impulse at delay
/// `distance / speedOfSound`, scaled by `1/distance` and the product of
/// per-bounce reflection factors `√(1−αᵢ)`. Beyond order K, a statistical
/// exponentially-decaying-noise late tail (deterministic per probe) whose
/// envelope reaches −60 dB over the room's Sabine RT60 stands in for the
/// diffuse field.
///
/// The direct (order-0) path is deliberately omitted — this IR feeds the reverb
/// aux slot, where the dry signal is the source's own. Output is float PCM in
/// the [-1,1] convention at `params.sampleRate` (physical amplitudes, not
/// peak-normalised — the inter-reflection levels are meaningful and the AX2 wet
/// gain is calibrated against them).
std::vector<float> bakeProbeIr(const std::vector<ReflectingFacet>& facets,
                               const glm::vec3& probePos,
                               float roomVolumeM3,
                               const BakeParams& params);

} // namespace Vestige
