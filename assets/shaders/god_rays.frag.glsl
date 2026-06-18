// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file god_rays.frag.glsl
/// @brief Phase 10 slice 11.5 — screen-space god rays (crepuscular light
///        shafts), Kenny Mitchell's radial-blur method (GPU Gems 3 ch. 13).
///
/// Half-resolution gather pass: from each pixel, step toward the sun's screen
/// position accumulating a *light buffer* that is the HDR scene radiance only
/// where the pixel is sky (reverse-Z depth ≈ 0); geometry contributes 0, so it
/// occludes the shafts. The result is additively combined into the HDR scene
/// (before bloom) by `god_rays_combine.frag.glsl`.
///
/// Runs only when the volumetric froxel path is NOT producing god rays (the
/// renderer gates it). The look constants below are provisional — exposed
/// per-scene by the Fog editor panel (slice 11.10).
#version 450 core

in  vec2 v_texCoord;
out vec4 FragColor;

uniform sampler2D u_sceneTexture; // pre-bloom HDR scene (unit 0)
uniform sampler2D u_depthTexture; // resolved depth, reverse-Z (point-sampled)
uniform vec2      u_sunUV;        // sun screen-UV (radial-blur centre)
uniform float     u_intensity;    // on-screen edge fade in [0,1]; 0 → no shafts

// Provisional look constants (Mitchell GPU Gems 3 defaults as a starting
// point). TODO 11.10 / Formula Workbench: expose per-scene via the Fog panel.
const int   NUM_SAMPLES = 64;
const float DENSITY  = 0.9;   // shaft length (fraction of the pixel→sun span)
const float DECAY    = 0.95;  // per-tap exponential falloff
const float WEIGHT   = 0.5;   // per-tap contribution
const float EXPOSURE = 0.3;   // overall shaft brightness

// Light buffer: scene radiance where the pixel is sky (reverse-Z depth ≈ 0,
// matching contact_shadows.frag.glsl), else 0. Depth is point-sampled
// (texelFetch) so the half-res gather doesn't mis-classify silhouettes by
// bilinear-filtering a non-linear reverse-Z depth. Out-of-frame taps give 0.
vec3 lightAt(vec2 uv)
{
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
    {
        return vec3(0.0);
    }
    ivec2 sz = textureSize(u_depthTexture, 0);
    ivec2 px = clamp(ivec2(uv * vec2(sz)), ivec2(0), sz - 1);
    float depth = texelFetch(u_depthTexture, px, 0).r;
    return depth < 0.0001 ? texture(u_sceneTexture, uv).rgb : vec3(0.0);
}

void main()
{
    if (u_intensity <= 0.0)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec2  uv    = v_texCoord;
    vec2  delta = (uv - u_sunUV) * (DENSITY / float(NUM_SAMPLES));
    vec2  coord = uv;
    float illum = 1.0;
    vec3  accum = lightAt(uv);

    for (int i = 1; i < NUM_SAMPLES; ++i)
    {
        coord -= delta;                       // step toward the sun
        accum += lightAt(coord) * illum * WEIGHT;
        illum *= DECAY;
    }

    FragColor = vec4(accum * (EXPOSURE * u_intensity), 1.0);
}
