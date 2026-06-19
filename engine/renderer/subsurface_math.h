// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file subsurface_math.h
/// @brief CPU spec for the subsurface-scattering math (Slice R3), pinned against the GLSL.
///
/// The two SSS terms added to the PBR light functions in scene.frag.glsl
/// (`sssFrontScatter`, `sssBackScatter`) are mirrored here as pure functions so a
/// GL-free parity test can pin the math (project Rule 7: dual CPU-spec / GPU-runtime
/// impl with a parity test). The four SSS_* tuning constants are the single source of
/// truth shared by both sides — the GLSL declares the same literals as file-scope
/// `const`s (design §10.2).
///
/// TODO: revisit the SSS_* look constants via Formula Workbench once a reference
/// SSS capture exists (design §10.5 — Rule 6 carve-out: no ground-truth dataset today).
#pragma once

#include <glm/glm.hpp>

#include <cmath>

namespace Vestige
{

/// Maximum terminator-wrap width (reached at strength = 1).
constexpr float SSS_MAX_WRAP = 0.5f;
/// Normal-perturbation of the back-light direction.
constexpr float SSS_DISTORTION = 0.2f;
/// Back-scatter lobe tightness.
constexpr float SSS_POWER = 4.0f;
/// Back-scatter master scale (future tuning knob; fixed at 1.0).
constexpr float SSS_SCALE = 1.0f;

/// @brief Front-scatter: colored wrap diffuse (the bleed added to the Lambert diffuse).
///
/// Takes the **raw, signed** N·L (NOT the clamped `max(dot,0)` the call sites hold) so
/// the wrap can bleed past the terminator into the shadow side. Returns the colored
/// bleed `color * max(wrapped - lambert, 0)`, which the shader folds into the diffuse
/// irradiance. Zero at `rawNdotL = 1` (pole) and again at `rawNdotL = -wrap`; positive
/// between, peaking at the terminator. Never negative.
///
/// @param rawNdotL Signed dot(N, L).
/// @param strength Subsurface strength in [0,1]; 0 ⇒ returns (0,0,0).
/// @param color    Subsurface tint.
inline glm::vec3 sssFrontScatter(float rawNdotL, float strength, const glm::vec3& color)
{
    float lambert = glm::max(rawNdotL, 0.0f);
    float wrap = strength * SSS_MAX_WRAP;
    float wrapped = glm::max((rawNdotL + wrap) / (1.0f + wrap), 0.0f);
    return color * glm::max(wrapped - lambert, 0.0f);
}

/// @brief Back-scatter: thickness-driven translucency (the glow through a backlit thin face).
///
/// Mirrors the Barré-Brisebois & Bouchard transmission term, with the R3 deviation that
/// `L + N*distortion` is normalized first (so the pre-pow value is a true cosine).
/// Returns `color * radiance * transmit`; 0 when strength = 0, thickness = 1, or the
/// viewer is on the lit side.
///
/// @param V         View direction (surface → camera), normalized.
/// @param L         Light direction (surface → light), normalized.
/// @param N         Surface normal, normalized.
/// @param strength  Subsurface strength in [0,1].
/// @param thickness Thickness proxy in [0,1]; transmission ∝ (1 - thickness).
/// @param color     Subsurface tint.
/// @param radiance  Incoming light radiance (already attenuation/cone-scaled by the caller).
inline glm::vec3 sssBackScatter(const glm::vec3& V, const glm::vec3& L, const glm::vec3& N,
                                float strength, float thickness,
                                const glm::vec3& color, const glm::vec3& radiance)
{
    glm::vec3 backLightDir = glm::normalize(L + N * SSS_DISTORTION);
    float backNdV = std::pow(glm::clamp(glm::dot(V, -backLightDir), 0.0f, 1.0f), SSS_POWER) * SSS_SCALE;
    float transmit = backNdV * strength * (1.0f - thickness);
    return color * radiance * transmit;
}

}  // namespace Vestige
