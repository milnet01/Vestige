// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fog.h
/// @brief Phase 10 fog primitives — distance fog, exponential height fog,
///        and directional sun inscatter (the "glow toward the sun"
///        lobe of atmospheric haze).
///
/// Three pure-function layers that the renderer composes into the final
/// HDR composite pass. All formulas are closed-form — no coefficients
/// to fit, so CLAUDE.md Rule 11 (Formula Workbench) doesn't apply.
///
/// ### 1. Distance fog
///
///   - Linear                     factor = (end - d) / (end - start)
///   - Exponential (GL_EXP)       factor = exp(-density * d)
///   - Exponential-squared (GL_EXP2)
///                                factor = exp(-(density * d)^2)
///
/// `factor` is the **visibility of the surface** (1.0 = fully visible,
/// 0.0 = fully fogged). The shader composes via
/// `mix(fogColour, surfaceColour, factor)`.
///
/// Reference: OpenGL Programming Guide §9; D3D9 fog-formulas
/// documentation at learn.microsoft.com/windows/win32/direct3d9.
///
/// ### 2. Exponential height fog (Quílez analytic integral)
///
/// World-space density `d(y) = a * exp(-b * (y - fogHeight))`.
/// The view-ray integral from camera origin `ro` in direction `rd` over
/// distance `t` is *closed form*:
///
///   fogAmount(t) = (a / b) * exp(-b * (ro.y - fogHeight))
///                 * (1 - exp(-b * rd.y * t)) / rd.y
///   transmittance = exp(-fogAmount(t))
///
/// The `rd.y == 0` case (horizontal view ray) collapses analytically to
///   fogAmount(t) = a * t * exp(-b * (ro.y - fogHeight))
/// which this module handles as a separate branch to avoid a
/// divide-by-zero.
///
/// Reference: Quílez 2010 "Better Fog / Colored Fog"
/// (iquilezles.org/articles/fog). The same closed form underlies
/// Unreal's Exponential Height Fog and HDRP's fog override.
///
/// ### 3. Sun inscatter lobe
///
/// Directional brightening of the fog colour toward the sun direction,
/// evaluated per pixel:
///
///   lobe = pow(max(dot(viewDir, -sunDir), 0), exponent)
///   fogColour = mix(baseFogColour, sunFogColour, lobe)
///
/// Canonical UE "DirectionalInscatteringColor" pattern. The exponent is
/// an aesthetic parameter (no reference data to fit).
///
/// ### CPU / GPU parity
///
/// The CPU path exists for editor previews, debug overlays, and tests;
/// the GPU path is `screen_quad.frag.glsl`. Both must agree byte-for-byte
/// at the surface of each primitive — tests in `test_fog.cpp` enforce
/// this by reproducing the GLSL `mix()` composite via `applyFog`.
#pragma once

#include "accessibility/post_process_accessibility.h"

#include <glm/vec3.hpp>

namespace Vestige
{

// -----------------------------------------------------------------------
// Distance fog
// -----------------------------------------------------------------------

/// @brief Distance-fog curve selector. Stored per-scene and pushed to
///        the composite shader as an int uniform. `None` is the default.
enum class FogMode
{
    None,                 ///< Pass-through — `computeFogFactor` returns 1.0.
    Linear,               ///< Linear ramp between `start` and `end`.
    Exponential,          ///< `exp(-density * d)` — GL_EXP.
    ExponentialSquared,   ///< `exp(-(density * d)^2)` — GL_EXP2, softer onset.
};

/// @brief Scene-wide distance-fog parameters. Units: engine metres.
struct FogParams
{
    /// Linear-RGB inscattering colour — kept in linear space so it
    /// composes with HDR lighting before tonemap.
    glm::vec3 colour = glm::vec3(0.55f, 0.60f, 0.65f);

    /// Linear mode: distance at which fog begins. Below this, surfaces
    /// are fully visible.
    float start = 20.0f;

    /// Linear mode: distance at which fog fully obscures the surface.
    /// Must be > `start`; degenerate spans return pass-through gain.
    float end = 200.0f;

    /// EXP/EXP2 density. Outdoor haze 0.005–0.02, pea-soup 0.05–0.10.
    float density = 0.02f;
};

/// @brief Computes surface-visibility factor for a fragment at eye-space
///        `distance` under `mode`. Returns a value in `[0, 1]`.
///        Negative / zero distance returns 1.0 (fog never projects
///        behind the camera plane).
float computeFogFactor(FogMode mode,
                       const FogParams& params,
                       float distance);

/// @brief Stable human-readable label for `mode`. Used by the editor's
///        fog inspector and by regression tests.
const char* fogModeLabel(FogMode mode);

// -----------------------------------------------------------------------
// Exponential height fog (Quílez analytic integral)
// -----------------------------------------------------------------------

/// @brief Height-fog parameters. Applied on top of distance fog.
///
/// The density at world-space altitude `y` is
///   `d(y) = groundDensity * exp(-heightFalloff * (y - fogHeight))`.
/// The view-ray integral from camera origin to fragment is closed form;
/// see the file header for the derivation.
struct HeightFogParams
{
    /// Linear-RGB inscattering colour for the height layer (often
    /// warmer than the distance-fog colour — dust near the ground).
    glm::vec3 colour = glm::vec3(0.65f, 0.55f, 0.45f);

    /// Altitude `y` at which density == `groundDensity`. Higher values
    /// raise the fog layer off the terrain.
    float fogHeight = 0.0f;

    /// Density at `fogHeight`. Typical 0.02 – 0.1 for visible haze.
    float groundDensity = 0.05f;

    /// Vertical falloff rate. Larger = tighter vertical transition
    /// between foggy valley and clear peaks. Typical 0.2 – 1.0.
    float heightFalloff = 0.5f;

    /// Optional max opacity clamp — matches UE `FogMaxOpacity`. 1.0
    /// means no clamp. 0.9 prevents the sky horizon from fully
    /// fading out on very long sightlines.
    float maxOpacity = 1.0f;
};

/// @brief Analytic height-fog transmittance for a view ray.
///
/// @param params      Height-fog parameters.
/// @param cameraY     Camera world-space Y.
/// @param rayDirY     Y component of the (unit-length) view direction.
/// @param rayLength   Distance from camera to fragment (metres).
/// @returns Transmittance in `[1 - maxOpacity, 1]`. 1.0 = no fog,
///          lower values = heavier fog.
float computeHeightFogTransmittance(const HeightFogParams& params,
                                    float cameraY,
                                    float rayDirY,
                                    float rayLength);

// -----------------------------------------------------------------------
// Directional sun inscatter lobe
// -----------------------------------------------------------------------

/// @brief Sun-direction inscatter lobe. Brightens fog colour toward the
///        sun, reading as "haze glow" near sunset / sunrise.
struct SunInscatterParams
{
    /// Sun-direction colour — what the fog tints toward when the view
    /// aligns with the sun. Typically warm (orange / gold).
    glm::vec3 colour = glm::vec3(1.0f, 0.85f, 0.55f);

    /// Cosine-lobe exponent — larger = tighter lobe around the sun.
    /// UE default is 4.0; values 2–16 look plausible.
    float exponent = 4.0f;

    /// Below this view distance the sun lobe is zero. Prevents the
    /// whole near-field from glowing when the camera looks at the sun.
    float startDistance = 5.0f;
};

/// @brief Computes sun-inscatter lobe weight.
///
/// @param params          Sun-inscatter parameters.
/// @param viewDir         Unit view direction from camera through pixel.
/// @param sunDirection    Unit direction *from* the scene *to* the sun
///                        (matches the directional-light convention).
/// @param viewDistance    Eye-space distance to the surface.
/// @returns Lobe weight in `[0, 1]` — 0 at start distance / backlit,
///          rising toward 1 as view aligns with sun.
float computeSunInscatterLobe(const SunInscatterParams& params,
                              const glm::vec3& viewDir,
                              const glm::vec3& sunDirection,
                              float viewDistance);

// -----------------------------------------------------------------------
// Composite helpers
// -----------------------------------------------------------------------

/// @brief CPU-side mirror of GLSL `mix(fogColour, surfaceColour, factor)`.
///        Clamps `factor` to `[0, 1]`.
///
/// Used by debug overlays, tests, and the editor preview. The runtime
/// render path does the identical mix on the GPU.
glm::vec3 applyFog(const glm::vec3& surfaceColour,
                   const glm::vec3& fogColour,
                   float factor);

/// @brief Full fog composite — distance + height + sun-inscatter in one
///        call. Mirrors the GLSL composition order in
///        `assets/shaders/screen_quad.frag.glsl` so the CPU and GPU
///        paths cannot drift.
///
/// Composition rules (see docs/PHASE10_FOG_DESIGN.md §4):
///   1. Distance fog colour is warped toward the sun tint by the
///      cosine lobe when `sunInscatterEnabled`. Height fog keeps its
///      own colour so ground mist doesn't inherit the sun glow.
///   2. Surface is first mixed with the warped distance-fog colour by
///      the distance-fog visibility factor.
///   3. Result is then mixed with the height-fog colour by the
///      height-fog transmittance.
///
/// `cameraWorldPos` and `worldPos` must be in world space. Returns a
/// colour in linear HDR (no tonemap / gamma applied — that happens
/// downstream).
struct FogCompositeInputs
{
    FogMode                   fogMode            = FogMode::None;
    FogParams                 fogParams;

    bool                      heightFogEnabled   = false;
    HeightFogParams           heightFogParams;

    bool                      sunInscatterEnabled = false;
    SunInscatterParams        sunInscatterParams;
    glm::vec3                 sunDirection        = glm::vec3(0.0f, -1.0f, 0.0f);

    glm::vec3                 cameraWorldPos      = glm::vec3(0.0f);
};

glm::vec3 composeFog(const glm::vec3& surfaceColour,
                     const FogCompositeInputs& inputs,
                     const glm::vec3& worldPos);

// -----------------------------------------------------------------------
// Accessibility transform (slice 11.9)
// -----------------------------------------------------------------------

/// @brief Snapshot of every authorable fog parameter. Used as both the
///        input ("authored") and output ("effective") of the
///        accessibility transform so callers can pass the same struct
///        through and diff the result.
///
/// Runtime-only fields that are *not* accessibility-controlled
/// (`sunDirection`, `cameraWorldPos`, depth texture bindings) live on
/// `FogCompositeInputs` instead. Splitting the concerns lets the
/// transform stay a pure function of scene-authored data + user
/// preferences.
struct FogState
{
    FogMode             fogMode             = FogMode::None;
    FogParams           fogParams;

    bool                heightFogEnabled    = false;
    HeightFogParams     heightFogParams;

    bool                sunInscatterEnabled = false;
    SunInscatterParams  sunInscatterParams;
};

/// @brief Applies `PostProcessAccessibilitySettings` to author-set fog
///        parameters and returns the effective state the shader should
///        consume this frame.
///
/// Transform rules:
///   - `fogEnabled == false` → every layer off, intensity scale and
///     reduce-motion ignored. Master disable beats everything else so
///     the one-click accessibility toggle is unambiguous.
///   - `fogIntensityScale`  — applied per-layer:
///       * Linear distance fog: `end` is pushed outward by
///         `start + span / scale` so scale = 0.5 halves the perceived
///         density, scale = 1 is identity. Scale ≤ 0.001 collapses to
///         `FogMode::None` (avoids a divide-by-zero and yields the
///         expected "no fog" experience).
///       * Exponential / Exponential² distance fog: density is
///         multiplied by scale. Scale = 0 → density 0 (no fog).
///       * Height fog: `groundDensity` and `maxOpacity` are both
///         multiplied so scale = 0 fully disables the layer.
///       * Sun inscatter: colour is multiplied so the lobe dims
///         proportionally (the shape / exponent stays authored).
///   - `reduceMotionFog == true` — the sun-inscatter lobe colour is
///     further halved so rapid camera pans past the sun can't produce
///     a hard flash (matches the Xbox / WCAG 2.2 SC 2.3.3 guidance for
///     photosensitivity-safe modes). Distance + height fog are
///     frame-static so the flag is a no-op for them today; volumetric
///     fog will consult it when slice 11.6 ships.
///
/// The function is total — any degenerate scale / missing layer is
/// handled with pass-through behaviour rather than throwing.
FogState applyFogAccessibilitySettings(
    const FogState& authored,
    const PostProcessAccessibilitySettings& settings);

} // namespace Vestige
