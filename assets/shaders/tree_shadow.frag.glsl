// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_shadow.frag.glsl
/// @brief Tree shadow-caster fragment shader — alpha-tests leaf cutouts so only
///        leaf shapes cast, and writes RSM flux beside the hardware depth to
///        match the directional shadow map's MRT (shadow_depth.frag contract,
///        Phase 13 G1). Design §4.4/T4, 3D_E-0033.
#version 450 core

in vec2 v_texCoord;
in vec3 v_worldNormal;

// Albedo (unit 0, like the scene/foliage caster): factor × optional texture.
uniform sampler2D u_texture;
uniform bool u_hasTexture;
uniform bool u_useAlphaTest;   // leaf materials (AlphaMode::MASK)
uniform float u_alphaCutoff;
uniform vec3 u_albedoFactor;

// Directional light: radiance (colour × intensity) + travel direction.
uniform vec3 u_lightRadiance;
uniform vec3 u_lightDir;

// RSM flux: albedo · radiance · max(0,N·L). RGB = flux, A = coverage marker.
layout(location = 0) out vec4 fluxOut;

void main()
{
    vec3 albedo = u_albedoFactor;
    if (u_hasTexture)
    {
        vec4 texel = texture(u_texture, v_texCoord);
        // Leaf cutout — discard transparent texels so only the leaf silhouette
        // casts (bark is opaque: u_useAlphaTest false → no discard).
        if (u_useAlphaTest && texel.a < u_alphaCutoff)
        {
            discard;
        }
        albedo *= texel.rgb;
    }

    vec3 N = normalize(v_worldNormal);
    vec3 L = normalize(-u_lightDir);
    float nDotL = max(0.0, dot(N, L));
    fluxOut = vec4(albedo * u_lightRadiance * nDotL, 1.0);
    // Depth is written automatically by the fixed-function depth test.
}
