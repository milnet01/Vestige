// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_fog.h
/// @brief Phase 10 slice 11.6 — froxel-grid coordinate system for
///        volumetric fog (frustum-aligned 3D texture, "froxels" =
///        frustum voxels).
///
/// This header holds the *pure-function* froxel grid math: the mapping
/// between a froxel's integer `(i, j, k)` coordinate and the view space
/// it represents. It is the CPU spec that pins the GPU compute shaders
/// (`volumetric_inject/scatter/integrate.comp`) — the shaders reproduce
/// these formulas, and the unit tests in `test_volumetric_fog.cpp`
/// enforce agreement (CLAUDE.md Rule 7, CPU spec ↔ GPU runtime parity).
///
/// The GL-resource ownership (3D textures, compute dispatch) lives in the
/// `VolumetricFog` subsystem class, added in a later step of slice 11.6;
/// the math below has no GL dependency so it tests headlessly.
///
/// ### Depth-slice distribution
///
/// Froxel depth slices are distributed *exponentially* along the view
/// ray so near-camera froxels are small (where detail matters) and far
/// froxels coarse. For a grid of `N` depth slices spanning view-space
/// linear depth `[near, far]`:
///
///   viewDepth(slice) = near * (far / near) ^ ((slice + 0.5) / N)
///
/// evaluated at each slice *centre* (`slice + 0.5`). The inverse maps a
/// view-space depth back to a (fractional) slice index for sampling:
///
///   slice(z) = N * log(z / near) / log(far / near) - 0.5
///
/// This is the standard Wronski-2014 / Frostbite froxel distribution
/// (design doc §4.1; research doc §3). Screen-tile mapping is linear:
/// froxel `(i, j)` covers the screen-UV cell centred at
/// `((i + 0.5) / resX, (j + 0.5) / resY)`.
#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Vestige
{

/// @brief Froxel grid dimensions + the view-depth range it spans.
///
/// Defaults match the design doc: 160 × 90 × 64 (= 921,600 froxels,
/// ~14 MB at RGBA16F). `near`/`far` are the *volumetric* range in
/// view-space metres. The composite clamps the depth-slice coord at `far`,
/// so pixels beyond it take the farthest froxel's integrated value; `far`
/// is therefore typically shorter than the camera far plane.
struct FroxelGridConfig
{
    int   resX = 160;     ///< Screen-tile columns. Must be > 0.
    int   resY = 90;      ///< Screen-tile rows. Must be > 0.
    int   resZ = 64;      ///< Depth slices. Must be > 0.
    float near = 0.5f;    ///< Near view-depth of the volume. Must be > 0.
    float far  = 200.0f;  ///< Far view-depth of the volume. Must be > near.
};

/// @brief Total froxel count for @p cfg (resX * resY * resZ).
int froxelCount(const FroxelGridConfig& cfg);

/// @brief View-space linear depth at the *centre* of depth slice @p slice.
///
/// Exponential distribution (see file header). `slice` is clamped to
/// `[0, resZ - 1]`; the returned depth is always in `[near, far]`.
/// Degenerate configs (`near <= 0`, `far <= near`, `resZ <= 0`) return
/// `near` (or a sane clamp) rather than producing NaN/Inf.
float froxelSliceToViewDepth(const FroxelGridConfig& cfg, int slice);

/// @brief Inverse of @ref froxelSliceToViewDepth — the (fractional) depth
///        slice index a view-space depth @p viewDepth maps to.
///
/// `viewDepth` is clamped to `[near, far]` before mapping, so the result
/// is always in `[-0.5, resZ - 0.5]`. Used by the composite to look up a
/// pixel's froxel. Degenerate configs return 0.
float viewDepthToFroxelSlice(const FroxelGridConfig& cfg, float viewDepth);

/// @brief View-space linear depth at depth-slice *boundary* index @p boundary.
///
/// Unlike @ref froxelSliceToViewDepth (which evaluates at slice *centres*,
/// `slice + 0.5`), this evaluates at integer boundaries: boundary `b` is the
/// near edge of slice `b` and the far edge of slice `b - 1`. Used by the
/// integrate pass to compute each slice's thickness for Beer-Lambert
/// accumulation, and is the CPU spec that pins
/// `volumetric_integrate.comp.glsl`. `boundary` is clamped to `[0, resZ]`;
/// `boundary(0) == near`, `boundary(resZ) == far`. Degenerate configs return
/// `near`.
float froxelSliceBoundaryViewDepth(const FroxelGridConfig& cfg, int boundary);

/// @brief Henyey-Greenstein phase function p(cosTheta; g), normalised so its
///        integral over the unit sphere is 1.
///
/// `g` is the anisotropy in (-1, 1): 0 = isotropic (returns `1/(4π)`),
/// positive = forward-scattering. This is the CPU reference that pins the
/// `henyeyGreenstein` GLSL helper in `volumetric_scatter.comp.glsl`
/// (CLAUDE.md Rule 7). The denominator is floored at a small epsilon so
/// `g → ±1` stays finite.
float henyeyGreensteinPhase(float cosTheta, float g);

/// @brief Screen-space UV (in [0,1]) at the centre of froxel column
///        `(i, j)`. Linear tiling: `((i + 0.5)/resX, (j + 0.5)/resY)`.
glm::vec2 froxelToScreenUV(const FroxelGridConfig& cfg, int i, int j);

/// @brief Fog density-noise controls (slice 11.8) — modulates the uniform
///        froxel medium with a drifting value-noise field so fog reads as
///        non-uniform haze. `enabled` defaults off (the renderer enables it
///        with provisional look constants until the editor panel, slice 11.10).
struct FogNoiseParams
{
    bool      enabled      = false;                   ///< Off until tuned per scene.
    float     frequency    = 0.05f;                   ///< Cycles per world metre.
    float     strength     = 0.6f;                    ///< 0..1 modulation depth.
    int       octaves      = 3;                       ///< FBM octaves (clamped 1..5).
    glm::vec3 windVelocity = glm::vec3(0.4f, 0.0f, 0.1f); ///< World m/s domain scroll.
};

/// @brief Density multiplier (mean ≈ 1) for a froxel at @p worldPos and
///        @p time, from a 3-octave value-noise FBM.
///
/// Returns `clamp(1 + strength·(2·n − 1), 0, 2)` with `n ∈ [0,1]` the FBM
/// value at `worldPos·frequency + windVelocity·time`. `strength == 0` ⇒ 1.
/// This is the CPU spec that pins the GLSL `fogDensityNoise` in
/// `volumetric_inject.comp.glsl` (Rule 7); the integer-hash core is
/// bit-reproducible CPU↔GLSL, the interpolated value matches within a few
/// ULPs. `octaves` is clamped to `[1, 5]`. Pure function — does not consult
/// `params.enabled` (the caller gates application).
float fogDensityNoise(const glm::vec3& worldPos, const FogNoiseParams& params, float time);

/// @brief Max active mist/ground-fog volumes uploaded to the inject SSBO
///        (slice 11.11). Over-cap volumes are dropped with a logged warning
///        (CLAUDE.md "no silent caps"). 32 keeps the SSBO at 2 KB.
constexpr int MAX_FOG_VOLUMES = 32;

/// @brief Shape of a placeable fog volume (slice 11.11).
enum class FogVolumeShape
{
    Box    = 0, ///< Axis-aligned box; `halfExtents` is the per-axis half-size.
    Sphere = 1  ///< Sphere; `halfExtents.x` is the radius (.y/.z ignored).
};

/// @brief A localized, placeable mist / ground-fog volume (slice 11.11) — e.g.
///        morning mist around the Bronze Laver or dust near the altar. Each
///        volume adds tinted extinction/scattering to the froxels it overlaps,
///        with a soft edge and optional animated turbulence.
struct FogVolume
{
    FogVolumeShape shape        = FogVolumeShape::Box;
    glm::vec3      center       = {0.0f, 0.0f, 0.0f};
    glm::vec3      halfExtents  = {1.0f, 1.0f, 1.0f}; ///< Box: per-axis half-size; Sphere: .x = radius.
    glm::vec3      colour       = {0.6f, 0.62f, 0.65f}; ///< Linear-RGB scattering tint.
    float          density      = 0.5f;  ///< Added extinction (1/m) at the volume core.
    float          edgeSoftness = 0.2f;  ///< 0..1 fraction of the extent over which density falls to 0.
    float          animSpeed    = 0.0f;  ///< Turbulence scroll speed (0 = static).
};

/// @brief Spatial density multiplier in [0,1] for a world-space sample inside
///        @p v at @p time — the CPU spec that pins the GLSL `fogVolumeDensity`
///        in `volumetric_inject.comp.glsl` (CLAUDE.md Rule 7).
///
/// 1 at the volume core, smoothly falling to 0 at the outer extent over the
/// `edgeSoftness` band (hard step when `edgeSoftness == 0`). When
/// `animSpeed != 0` the result is multiplied by a value-noise-FBM turbulence
/// term (the same integer-hash field as @ref fogDensityNoise), so animated mist
/// reads as wispy and churning. The inject pass scales this by `v.density`.
/// Pure function — the caller multiplies in `density`/`colour`.
float fogVolumeDensity(const FogVolume& v, const glm::vec3& worldPos, float time);

/// @brief Where the sun projects on screen + an on-screen fade, for the
///        screen-space god-rays pass (slice 11.5).
struct GodRaySunScreen
{
    bool      visible   = false;        ///< Sun is in front of the camera and not fully faded out.
    glm::vec2 uv        = {0.5f, 0.5f}; ///< Sun screen-UV in [0,1] (the radial-blur centre).
    float     intensity = 0.0f;         ///< Edge fade: 1 on-screen, →0 past the frame, 0 if behind.
};

/// @brief Project a directional light (a point at infinity) to screen space
///        and compute an edge fade, for the god-rays radial blur (slice 11.5).
///
/// @p lightDirection is the light's *travel* direction (as stored on the
/// directional light); it is negated internally to point toward the sun, to
/// match the froxel scatter pass. `clip = projection · view · vec4(toward-sun,
/// 0)`; `visible` is false (and `intensity` 0) when the sun is behind the
/// camera (`clip.w ≤ 0`). `intensity` fades from 1 inside the frame to 0 over
/// an @p edgeMargin band past the screen edge (a hard cut at the edge when
/// `edgeMargin == 0`), so the shafts don't pop as the sun crosses the frustum.
/// Pure function — the renderer feeds `uv`/`intensity` to the gather shader.
GodRaySunScreen godRaysSunScreenInfo(const glm::mat4& view,
                                     const glm::mat4& projection,
                                     const glm::vec3& lightDirection,
                                     float edgeMargin);

} // namespace Vestige
