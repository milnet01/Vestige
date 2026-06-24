// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file foliage_shadow.frag.glsl
/// @brief Foliage shadow depth fragment shader — alpha test + RSM flux for
///        world-space GI (Phase 13 G1).
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_texture;

// Directional light radiance (colour × intensity) + travel direction.
uniform vec3 u_lightRadiance;
uniform vec3 u_lightDir;

layout(location = 0) out vec4 fluxOut;

void main()
{
    vec4 texel = texture(u_texture, v_texCoord);

    // Alpha test — discard transparent pixels so only grass blade shapes cast
    // shadows and write flux.
    if (texel.a < 0.5)
        discard;

    // Grass billboards (star meshes) have no meaningful per-fragment normal, so
    // approximate the blade as an upward-facing scatterer for the flux cosine —
    // a deliberate simplification for indirect light (Rule 5). The per-instance
    // colour tint from the main pass is not bound here; flux uses the atlas
    // albedo directly.
    const vec3 N = vec3(0.0, 1.0, 0.0);
    vec3 L = normalize(-u_lightDir);
    float nDotL = max(0.0, dot(N, L));

    fluxOut = vec4(texel.rgb * u_lightRadiance * nDotL, 1.0);
    // Depth is written automatically by the fixed-function pipeline.
}
