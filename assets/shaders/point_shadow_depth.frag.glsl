// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file point_shadow_depth.frag.glsl
/// @brief Point light shadow cubemap fragment shader — writes linear distance as
///        depth, plus RSM flux for world-space GI (Phase 13 G1).
#version 450 core

in vec3 v_fragPosition;
in vec3 v_worldNormal;
in vec2 v_texCoord;

uniform vec3 u_lightPos;
uniform float u_farPlane;

// Albedo (shared unit 0, like the scene pass): factor × optional texture.
uniform sampler2D u_diffuseTexture;
uniform bool u_hasTexture;
uniform vec3 u_albedoFactor;

// Point light: radiance (colour × intensity) + distance attenuation factors.
uniform vec3 u_lightRadiance;
uniform float u_attConstant;
uniform float u_attLinear;
uniform float u_attQuadratic;

// Reflective-shadow-map flux: albedo · radiance · max(0,N·L) · attenuation.
// Mirrors giRsmFluxPoint() in gi_probe_math.h.
layout(location = 0) out vec4 fluxOut;

void main()
{
    // Write linear distance from light as depth (normalized to [0,1]).
    // Note: gl_FragDepth disables early-Z/Hi-Z on RDNA2, but the alternative
    // (hardware depth + linearization in sampling shader) adds per-fragment cost
    // to the main scene pass and complicates cubemap edge comparison. For the
    // current max of 2 point shadow lights, this approach is acceptable.
    vec3 toLight = u_lightPos - v_fragPosition;
    float lightDistance = length(toLight);
    gl_FragDepth = lightDistance / u_farPlane;

    vec3 albedo = u_albedoFactor;
    if (u_hasTexture)
    {
        albedo *= texture(u_diffuseTexture, v_texCoord).rgb;
    }

    vec3 N = normalize(v_worldNormal);
    vec3 L = (lightDistance > 0.0) ? toLight / lightDistance : vec3(0.0);
    float nDotL = max(0.0, dot(N, L));
    float atten = 1.0 / (u_attConstant
                       + u_attLinear * lightDistance
                       + u_attQuadratic * lightDistance * lightDistance);

    fluxOut = vec4(albedo * u_lightRadiance * nDotL * atten, 1.0);
}
