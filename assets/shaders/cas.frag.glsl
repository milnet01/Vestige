// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cas.frag.glsl
/// @brief Contrast-Adaptive Sharpening — AMD FidelityFX CAS, sharpen-only
///        path (no scaling). A single full-screen 3x3 pass over the LDR
///        image that both claws back the softness of the render-scale
///        upscale AND counters FXAA's mild edge blur, so one pass serves
///        both features (design §3.2). Portable GLSL kernel, no motion
///        vectors, ~0.1 ms. Runs AFTER FXAA, before present.
///
/// Adaptive: the sharpening weight per pixel is scaled by how much local
/// headroom exists (min/max of the 3x3 neighbourhood), so flat regions and
/// already-saturated pixels are left alone while genuine edges are crisped —
/// this is what stops CAS from amplifying noise the way a fixed unsharp mask
/// would. Operates in the composited LDR (gamma) space, same as FXAA.
#version 450 core

uniform sampler2D u_colorTexture;
uniform float u_sharpness; // [0,1] — 0 gentlest, 1 strongest

in vec2 v_texCoord;

out vec4 fragColor;

void main()
{
    // 3x3 neighbourhood:
    //   a b c
    //   d e f
    //   g h i
    vec3 a = textureOffset(u_colorTexture, v_texCoord, ivec2(-1, -1)).rgb;
    vec3 b = textureOffset(u_colorTexture, v_texCoord, ivec2( 0, -1)).rgb;
    vec3 c = textureOffset(u_colorTexture, v_texCoord, ivec2( 1, -1)).rgb;
    vec3 d = textureOffset(u_colorTexture, v_texCoord, ivec2(-1,  0)).rgb;
    vec4 e = texture(u_colorTexture, v_texCoord);
    vec3 f = textureOffset(u_colorTexture, v_texCoord, ivec2( 1,  0)).rgb;
    vec3 g = textureOffset(u_colorTexture, v_texCoord, ivec2(-1,  1)).rgb;
    vec3 h = textureOffset(u_colorTexture, v_texCoord, ivec2( 0,  1)).rgb;
    vec3 i = textureOffset(u_colorTexture, v_texCoord, ivec2( 1,  1)).rgb;

    // Soft min/max: the plus (b,d,e,f,h) plus a second pass folding in the
    // corners — both accumulate to a doubled range (hence the 2.0 below).
    vec3 mnRGB  = min(min(min(d, e.rgb), min(f, b)), h);
    vec3 mnRGB2 = min(mnRGB, min(min(a, c), min(g, i)));
    mnRGB += mnRGB2;

    vec3 mxRGB  = max(max(max(d, e.rgb), max(f, b)), h);
    vec3 mxRGB2 = max(mxRGB, max(max(a, c), max(g, i)));
    mxRGB += mxRGB2;

    // Smooth amount of sharpening: distance to the nearest signal limit,
    // divided by the local peak. Zero where there is no headroom.
    vec3 rcpMRGB = 1.0 / max(mxRGB, vec3(1e-4));
    vec3 ampRGB  = clamp(min(mnRGB, 2.0 - mxRGB) * rcpMRGB, 0.0, 1.0);
    ampRGB = sqrt(ampRGB);

    // Filter shape: peak weight from the sharpness knob. mix(8,5) matches
    // the FidelityFX CAS "sharpness" mapping (5 = strongest, 8 = gentlest).
    float peak = -1.0 / mix(8.0, 5.0, clamp(u_sharpness, 0.0, 1.0));
    vec3 wRGB  = ampRGB * peak;
    vec3 rcpWeightRGB = 1.0 / (1.0 + 4.0 * wRGB);

    vec3 outColor = clamp(
        (b * wRGB + d * wRGB + f * wRGB + h * wRGB + e.rgb) * rcpWeightRGB,
        0.0, 1.0);

    fragColor = vec4(outColor, e.a);
}
