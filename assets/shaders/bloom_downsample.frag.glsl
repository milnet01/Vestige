// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file bloom_downsample.frag.glsl
/// @brief Bloom mip-chain downsampling with Karis average to suppress firefly artifacts.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_sourceTexture;
uniform vec2 u_srcTexelSize;    // 1.0 / source resolution
uniform bool u_useKarisAverage; // true only on first downsample (prevents fireflies)
uniform float u_threshold;      // Brightness threshold (only on first downsample)

out vec4 fragColor;

/// BT.709 perceptual luminance of a linear-RGB colour. Constants from
/// ITU-R Recommendation BT.709 §3.2 (HDTV primaries with D65 white).
/// Mirrored byte-for-byte by `bt709Luminance` in `tests/test_bloom.cpp`.
float bt709Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

/// Karis average weight — suppresses firefly artifacts on the first downsample
/// by weighting each quadrant inversely proportional to its luminance.
float karisWeight(vec3 color)
{
    return 1.0 / (1.0 + bt709Luminance(color));
}

/// Soft luminance-keyed bright pass. Pre-R# was an inline SOFT_THRESHOLD
/// macro; promoted to a function so CPU↔GPU parity tests can extract the
/// formula from this file without preprocessor games. `contrib / (luma +
/// epsilon)` rescales each component by the soft-knee factor while
/// preserving the colour ratio (per-component thresholding shifts hue).
vec3 softThreshold(vec3 color, float threshold)
{
    float luma    = bt709Luminance(color);
    float contrib = max(0.0, luma - threshold);
    contrib       = contrib / (contrib + 1.0);
    return color * (contrib / (luma + 0.0001));
}

void main()
{
    // 13-tap downsample filter from Call of Duty: Advanced Warfare (Jorge Jimenez).
    // Uses bilinear hardware filtering to cover a wide area with only 13 taps.
    // Pattern: 4 corner quads + 4 edge quads + 1 center quad, weighted for a
    // smooth, alias-free downsample that preserves energy.

    float x = u_srcTexelSize.x;
    float y = u_srcTexelSize.y;

    // Sanitize: clamp NaN/Inf from scene shader to prevent propagation through bloom chain.
    // NaN spreads through the 13-tap filter and upsample, creating black square artifacts.
    #define SANITIZE(s) s = clamp(s, vec3(0.0), vec3(65504.0))

    // 4 corner samples (2 texels away, bilinear gives 2x2 average each)
    vec3 a = texture(u_sourceTexture, v_texCoord + vec2(-2.0*x,  2.0*y)).rgb;
    vec3 b = texture(u_sourceTexture, v_texCoord + vec2( 0.0,    2.0*y)).rgb;
    vec3 c = texture(u_sourceTexture, v_texCoord + vec2( 2.0*x,  2.0*y)).rgb;

    vec3 d = texture(u_sourceTexture, v_texCoord + vec2(-2.0*x,  0.0)).rgb;
    vec3 e = texture(u_sourceTexture, v_texCoord + vec2( 0.0,    0.0)).rgb;
    vec3 f = texture(u_sourceTexture, v_texCoord + vec2( 2.0*x,  0.0)).rgb;

    vec3 g = texture(u_sourceTexture, v_texCoord + vec2(-2.0*x, -2.0*y)).rgb;
    vec3 h = texture(u_sourceTexture, v_texCoord + vec2( 0.0,   -2.0*y)).rgb;
    vec3 i = texture(u_sourceTexture, v_texCoord + vec2( 2.0*x, -2.0*y)).rgb;

    // 4 inner samples (1 texel away)
    vec3 j = texture(u_sourceTexture, v_texCoord + vec2(-x,  y)).rgb;
    vec3 k = texture(u_sourceTexture, v_texCoord + vec2( x,  y)).rgb;
    vec3 l = texture(u_sourceTexture, v_texCoord + vec2(-x, -y)).rgb;
    vec3 m = texture(u_sourceTexture, v_texCoord + vec2( x, -y)).rgb;

    SANITIZE(a); SANITIZE(b); SANITIZE(c);
    SANITIZE(d); SANITIZE(e); SANITIZE(f);
    SANITIZE(g); SANITIZE(h); SANITIZE(i);
    SANITIZE(j); SANITIZE(k); SANITIZE(l); SANITIZE(m);
    #undef SANITIZE

    // On the first downsample, apply luminance-based soft threshold to extract
    // only bright areas. Uses BT.709 luminance to preserve color ratios
    // (per-component threshold would shift colors, e.g. orange → red).
    if (u_useKarisAverage && u_threshold > 0.0)
    {
        a = softThreshold(a, u_threshold);
        b = softThreshold(b, u_threshold);
        c = softThreshold(c, u_threshold);
        d = softThreshold(d, u_threshold);
        e = softThreshold(e, u_threshold);
        f = softThreshold(f, u_threshold);
        g = softThreshold(g, u_threshold);
        h = softThreshold(h, u_threshold);
        i = softThreshold(i, u_threshold);
        j = softThreshold(j, u_threshold);
        k = softThreshold(k, u_threshold);
        l = softThreshold(l, u_threshold);
        m = softThreshold(m, u_threshold);
    }

    vec3 result;

    if (u_useKarisAverage)
    {
        // First downsample: apply Karis fireflies suppression composed
        // with the canonical Jimenez 2014 group weights (slide 147 —
        // 0.5 centre + 0.125 × 4 corners = 1.0, energy preserving).
        // Pre-R9 this dropped the fixed weights and treated all 5
        // groups equally weighted by Karis luminance only, undervaluing
        // the inner-4-sample group's high-frequency contribution and
        // producing "softness pop" between mip 0 and mip 1.
        // Mirrored byte-for-byte by combineBloomKarisGroups in
        // engine/renderer/bloom_downsample_karis.h (Rule 12).
        vec3 g0 = (a + b + d + e) * 0.25;  // top-left corner group
        vec3 g1 = (b + c + e + f) * 0.25;  // top-right corner group
        vec3 g2 = (d + e + g + h) * 0.25;  // bottom-left corner group
        vec3 g3 = (e + f + h + i) * 0.25;  // bottom-right corner group
        vec3 g4 = (j + k + l + m) * 0.25;  // inner / centre group

        float w0 = karisWeight(g0);
        float w1 = karisWeight(g1);
        float w2 = karisWeight(g2);
        float w3 = karisWeight(g3);
        float w4 = karisWeight(g4);

        const float CENTRE_WEIGHT = 0.5;
        const float CORNER_WEIGHT = 0.125;

        vec3 numerator =
            CENTRE_WEIGHT * (g4 * w4)
          + CORNER_WEIGHT * (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3);

        float denominator =
            CENTRE_WEIGHT * w4
          + CORNER_WEIGHT * (w0 + w1 + w2 + w3);

        result = numerator / denominator;
    }
    else
    {
        // Standard weighted downsample (energy preserving)
        //   0.5  for inner quad (j,k,l,m)
        //   0.125 for each of the 4 corner quads
        result  = e * 0.125;
        result += (a + c + g + i) * 0.03125;       // 0.125 / 4
        result += (b + d + f + h) * 0.0625;        // 0.25 / 4
        result += (j + k + l + m) * 0.125;         // 0.5 / 4
    }

    fragColor = vec4(result, 1.0);
}
