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
in float v_alpha;   // signed crossfade dissolve (T9)

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

// Copied from tree_mesh.frag (no shared GLSL #include) — the SAME dither the
// visible pass uses, so the shadow dissolves in lockstep with the canopy.
float interleavedGradientNoise(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main()
{
    // T9 signed screen-door dissolve — matches tree_mesh.frag so the ground shadow
    // crossfades/fades with the drawn canopy instead of snapping at tier boundaries.
    float dither = interleavedGradientNoise(gl_FragCoord.xy);
    if (v_alpha >= 0.0)
    {
        if (dither >= v_alpha) discard;   // outgoing / solid: keep noise < v_alpha
    }
    else
    {
        if (dither < -v_alpha) discard;   // incoming: keep noise >= |v_alpha|
    }

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
