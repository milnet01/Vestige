// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_shadow.frag.glsl
/// @brief GPU-grass shadow-caster fragment shader (3D_E-0042) — writes RSM flux
///        beside the hardware depth, matching the directional shadow map's MRT
///        (shadow_depth.frag contract, Phase 13 G1).
///
/// Pairs with the UNMODIFIED `grass.vert.glsl`: the shadow program links the same
/// vertex shader the visible pass uses, driven with the light-space matrix in
/// `u_viewProjection`. That is the whole anti-drift mechanism — there is exactly
/// one blade generator, so a blade's shadow cannot disagree with its silhouette.
///
/// Unlike the foliage and tree casters this shader has NO alpha test: a grass
/// blade is real opaque Bézier ribbon geometry, not a cut-out card, so there is
/// nothing to discard. Early-Z therefore stays enabled through the shadow pass
/// (docs/research/grass_shadows_research.md §2.2 — the `discard` cost that makes
/// billboard-grass shadow casting expensive simply does not apply here).
#version 450 core

// Subset of grass.vert.glsl's outputs — the unused ones (v_worldPos, v_viewDepth,
// v_heightAO) are shading-pass concerns and need no declaration here.
in vec3 v_normal;   // vertical-biased blade normal
in vec3 v_tint;     // root→tip green × per-clump drift (stands in for albedo)

// Directional light: radiance (colour × intensity) + travel direction.
uniform vec3 u_lightRadiance;
uniform vec3 u_lightDir;

// RSM flux: albedo · radiance · max(0,N·L). RGB = flux, A = coverage marker.
layout(location = 0) out vec4 fluxOut;

void main()
{
    // The blade carries a real per-fragment normal (the foliage caster has to fake
    // an upward one for its billboards), so the flux cosine is the true one.
    vec3 N = normalize(v_normal);
    vec3 L = normalize(-u_lightDir);
    float nDotL = max(0.0, dot(N, L));

    fluxOut = vec4(v_tint * u_lightRadiance * nDotL, 1.0);
    // Depth is written automatically by the fixed-function depth test.
}
