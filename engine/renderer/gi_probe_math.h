// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gi_probe_math.h
/// @brief CPU spec for the world-space GI probe math (Phase 13, DDGI-lite),
///        pinned against the GLSL.
///
/// This is the CPU twin of the world-space GI shader math, the same dual
/// CPU-spec / GPU-runtime discipline `gi_math.h` applies to the froxel near-field
/// GI (project Rule 7). It grows one slice at a time:
///   - G1 (here): the reflective-shadow-map **flux** term written by the shadow
///     pass — `albedo · light-radiance · max(0,N·L) [· attenuation]`. Pinned by
///     the flux-readback parity test (design §4 G1, §8).
///   - G2: SH projection / VPL scatter (to be added).
///   - G3: temporal EMA blend + Chebyshev visibility (to be added).
///
/// The flux convention is the design's (§2.1): `flux = albedo · incident-radiance`,
/// with the cosine fold-in (`max(0,N·L)`) so each shadow texel stores the
/// reflected radiosity of its surface as a virtual point light. The 1/π and
/// solid-angle factors are folded into the G2 injection, consistent with the
/// existing radiance-SH convention. RGB = flux, A = 1 where geometry wrote it.
#pragma once

#include <glm/glm.hpp>

#include <algorithm>

namespace Vestige
{

/// @brief RSM flux for a surface lit by a directional light (the G1 canonical
///        formula; CPU twin of `shadow_depth.frag.glsl`).
///
/// @param albedo     Surface base colour (linear).
/// @param radiance   Light radiance (colour × intensity; `DirectionalLight::diffuse`).
/// @param normal     World-space surface normal (need not be unit; normalised here).
/// @param lightDir   Direction the light *travels* (`DirectionalLight::direction`);
///                   the incidence vector `L` toward the light is its negation.
/// @return `vec4(flux.rgb, 1.0)` — RGB = `albedo · radiance · max(0,N·L)`, A = 1.
inline glm::vec4 giRsmFluxDirectional(const glm::vec3& albedo,
                                      const glm::vec3& radiance,
                                      const glm::vec3& normal,
                                      const glm::vec3& lightDir)
{
    glm::vec3 N = glm::normalize(normal);
    glm::vec3 L = glm::normalize(-lightDir);
    float nDotL = std::max(0.0f, glm::dot(N, L));
    return glm::vec4(albedo * radiance * nDotL, 1.0f);
}

/// @brief RSM flux for a surface lit by a point light (CPU twin of
///        `point_shadow_depth.frag.glsl`).
///
/// Adds distance attenuation to the directional form, matching the engine's
/// point-light falloff `1 / (constant + linear·d + quadratic·d²)`.
///
/// @param albedo     Surface base colour (linear).
/// @param radiance   Light radiance (`PointLight::diffuse`).
/// @param worldPos   World-space surface position.
/// @param normal     World-space surface normal (normalised here).
/// @param lightPos   World-space light position.
/// @param kc,kl,kq   Constant / linear / quadratic attenuation factors.
/// @return `vec4(flux.rgb, 1.0)` — RGB = `albedo · radiance · max(0,N·L) · atten`.
inline glm::vec4 giRsmFluxPoint(const glm::vec3& albedo,
                                const glm::vec3& radiance,
                                const glm::vec3& worldPos,
                                const glm::vec3& normal,
                                const glm::vec3& lightPos,
                                float kc, float kl, float kq)
{
    glm::vec3 toLight = lightPos - worldPos;
    float d = glm::length(toLight);
    glm::vec3 N = glm::normalize(normal);
    glm::vec3 L = (d > 0.0f) ? toLight / d : glm::vec3(0.0f);
    float nDotL = std::max(0.0f, glm::dot(N, L));
    float atten = 1.0f / (kc + kl * d + kq * d * d);
    return glm::vec4(albedo * radiance * nDotL * atten, 1.0f);
}

}  // namespace Vestige
