// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file shadow_depth.frag.glsl
/// @brief Directional shadow map fragment shader — writes RSM flux alongside the
///        hardware-written depth (Phase 13 G1, world-space GI).
#version 450 core

in vec3 v_worldNormal;
in vec2 v_texCoord;

// Albedo (shared unit 0, like the scene pass): factor × optional texture.
uniform sampler2D u_diffuseTexture;
uniform bool u_hasTexture;
uniform vec3 u_albedoFactor;

// Directional light: radiance (colour × intensity) + travel direction.
uniform vec3 u_lightRadiance;
uniform vec3 u_lightDir;

// Reflective-shadow-map flux: albedo · radiance · max(0,N·L). RGB = flux,
// A = 1 (coverage marker the GI inject pass uses to skip empty texels).
layout(location = 0) out vec4 fluxOut;

// CPU twin: giRsmFluxDirectional() in gi_probe_math.h. Extracted verbatim by the
// flux-readback parity test (test_gi_probe_gpu.cpp) so the two cannot drift —
// @p lightDir is the direction the light travels (incidence L is its negation).
vec4 giRsmFluxDirectional(vec3 albedo, vec3 radiance, vec3 normal, vec3 lightDir)
{
    vec3 N = normalize(normal);
    vec3 L = normalize(-lightDir);
    float nDotL = max(0.0, dot(N, L));
    return vec4(albedo * radiance * nDotL, 1.0);
}

void main()
{
    vec3 albedo = u_albedoFactor;
    if (u_hasTexture)
    {
        albedo *= texture(u_diffuseTexture, v_texCoord).rgb;
    }

    fluxOut = giRsmFluxDirectional(albedo, u_lightRadiance, v_worldNormal, u_lightDir);
    // Depth is written automatically by the fixed-function depth test.
}
