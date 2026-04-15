// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file bloom_bright.frag.glsl
/// @brief Bloom brightness extraction — isolates pixels above a luminance threshold with soft knee.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_hdrTexture;
uniform float u_threshold;

out vec4 fragColor;

// AUDIT.md §M15 / FIXPLAN F2: deep-hue bloom firefly clamp.
//
// A saturated-hue pixel (e.g. bright red: (100,0,0) at threshold 1) has
// luminance ≈ 21.26 but the color channel is 100; with the old epsilon
// 1e-4 the red channel amplifies to ~21.26× and combined with the soft
// knee contribution could reach five figures. This leaks a firefly into
// the blurred mipchain that dominates the final composite.
const float BLOOM_EPSILON = 1e-2;    // conservative: no 10000× amplification
const float MAX_BLOOM     = 256.0;   // hard clamp after divide

void main()
{
    vec3 color = texture(u_hdrTexture, v_texCoord).rgb;

    // Sanitize: clamp to prevent Inf/NaN propagation through bloom chain
    color = clamp(color, vec3(0.0), vec3(65504.0));

    // BT.709 luminance
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft knee: smooth transition instead of hard cutoff (reduces firefly artifacts)
    float contribution = max(0.0, luminance - u_threshold);
    contribution = contribution / (contribution + 1.0);
    vec3 bright = color * contribution / max(luminance, BLOOM_EPSILON);
    bright = min(bright, vec3(MAX_BLOOM));
    fragColor = vec4(bright, 1.0);
}
