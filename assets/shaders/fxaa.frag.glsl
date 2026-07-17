// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fxaa.frag.glsl
/// @brief Fast approximate anti-aliasing — Timothy Lottes' FXAA 3.11 PC
///        quality path at QUALITY__PRESET 39 (the widest edge search). A
///        single dependent full-screen pass over the composited LDR image:
///        no multisample buffer, no history, ~0.1-0.5 ms. The budget-tier
///        AA (Low/Medium presets) paired with the CAS sharpen that follows.
///        Structure follows the canonical reference edge-search + sub-pixel
///        implementation so the sign conventions are the well-tested ones.
///
/// Luma source: computed in-shader from the sRGB (gamma-space) LDR colour
/// via the Rec.601 luma weights. FXAA is designed to run in gamma space on
/// the post-tonemap image, so no linearisation is applied here (design §3.4).
///
/// Tuning constants (design §3.4 — the "sharp FXAA" defaults, authored as
/// named consts so they can become user settings later without a rewrite):
///   SUBPIX             0.333  — low sub-pixel aliasing removal, so it
///                               smooths EDGES not interior texture/HUD detail
///   EDGE_THRESHOLD     0.125  — min local contrast treated as an edge
///   EDGE_THRESHOLD_MIN 0.0833 — dark-region floor (Lottes' recommended pair)
#version 450 core

uniform sampler2D u_colorTexture;
uniform vec4 u_rtMetrics; // (1/width, 1/height, width, height)

in vec2 v_texCoord;

out vec4 fragColor;

const float FXAA_SUBPIX             = 0.333;
const float FXAA_EDGE_THRESHOLD     = 0.125;
const float FXAA_EDGE_THRESHOLD_MIN = 0.0833;

// QUALITY__PRESET 39 — 12 edge-search steps.
const int   FXAA_ITERATIONS = 12;
const float FXAA_QUALITY[12] = float[12](
    1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

// Rec.601 perceptual luma from a gamma-space RGB triple.
float rgb2luma(vec3 rgb)
{
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec2 inverseScreenSize = u_rtMetrics.xy; // (1/w, 1/h)
    vec2 uv = v_texCoord;

    vec3 colorCenter = texture(u_colorTexture, uv).rgb;
    float lumaCenter = rgb2luma(colorCenter);

    // Luma at the four direct neighbours.
    float lumaDown  = rgb2luma(textureOffset(u_colorTexture, uv, ivec2( 0, -1)).rgb);
    float lumaUp    = rgb2luma(textureOffset(u_colorTexture, uv, ivec2( 0,  1)).rgb);
    float lumaLeft  = rgb2luma(textureOffset(u_colorTexture, uv, ivec2(-1,  0)).rgb);
    float lumaRight = rgb2luma(textureOffset(u_colorTexture, uv, ivec2( 1,  0)).rgb);

    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    // Early-out on low-contrast pixels — nothing to anti-alias, and this is
    // what keeps busy micro-texture (grass/gravel) from being smeared.
    if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD))
    {
        fragColor = vec4(colorCenter, 1.0);
        return;
    }

    // Luma at the four corners.
    float lumaDownLeft  = rgb2luma(textureOffset(u_colorTexture, uv, ivec2(-1, -1)).rgb);
    float lumaUpRight   = rgb2luma(textureOffset(u_colorTexture, uv, ivec2( 1,  1)).rgb);
    float lumaUpLeft    = rgb2luma(textureOffset(u_colorTexture, uv, ivec2(-1,  1)).rgb);
    float lumaDownRight = rgb2luma(textureOffset(u_colorTexture, uv, ivec2( 1, -1)).rgb);

    float lumaDownUp    = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;
    float lumaLeftCorners  = lumaDownLeft + lumaUpLeft;
    float lumaDownCorners  = lumaDownLeft + lumaDownRight;
    float lumaRightCorners = lumaDownRight + lumaUpRight;
    float lumaUpCorners    = lumaUpRight + lumaUpLeft;

    // Edge orientation via the Sobel-like second derivatives.
    float edgeHorizontal =
        abs(-2.0 * lumaLeft   + lumaLeftCorners)  +
        abs(-2.0 * lumaCenter + lumaDownUp) * 2.0 +
        abs(-2.0 * lumaRight  + lumaRightCorners);
    float edgeVertical =
        abs(-2.0 * lumaUp     + lumaUpCorners)    +
        abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0 +
        abs(-2.0 * lumaDown   + lumaDownCorners);
    bool isHorizontal = (edgeHorizontal >= edgeVertical);

    // Gradient to the two neighbours perpendicular to the edge.
    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp   : lumaRight;
    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;
    bool  is1Steepest = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    // Step one texel toward the steeper side; average the luma there.
    float stepLength = isHorizontal ? inverseScreenSize.y : inverseScreenSize.x;
    float lumaLocalAverage = 0.0;
    if (is1Steepest)
    {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    }
    else
    {
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }

    vec2 currentUv = uv;
    if (isHorizontal) currentUv.y += stepLength * 0.5;
    else              currentUv.x += stepLength * 0.5;

    // March along the edge in both directions until the local gradient ends.
    vec2 offset = isHorizontal ? vec2(inverseScreenSize.x, 0.0)
                               : vec2(0.0, inverseScreenSize.y);
    vec2 uv1 = currentUv - offset * FXAA_QUALITY[0];
    vec2 uv2 = currentUv + offset * FXAA_QUALITY[0];

    float lumaEnd1 = rgb2luma(texture(u_colorTexture, uv1).rgb) - lumaLocalAverage;
    float lumaEnd2 = rgb2luma(texture(u_colorTexture, uv2).rgb) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) uv1 -= offset * FXAA_QUALITY[1];
    if (!reached2) uv2 += offset * FXAA_QUALITY[1];

    if (!reachedBoth)
    {
        for (int i = 2; i < FXAA_ITERATIONS; ++i)
        {
            if (!reached1)
                lumaEnd1 = rgb2luma(texture(u_colorTexture, uv1).rgb) - lumaLocalAverage;
            if (!reached2)
                lumaEnd2 = rgb2luma(texture(u_colorTexture, uv2).rgb) - lumaLocalAverage;
            reached1 = abs(lumaEnd1) >= gradientScaled;
            reached2 = abs(lumaEnd2) >= gradientScaled;
            reachedBoth = reached1 && reached2;

            if (!reached1) uv1 -= offset * FXAA_QUALITY[i];
            if (!reached2) uv2 += offset * FXAA_QUALITY[i];
            if (reachedBoth) break;
        }
    }

    // Distance to each end of the edge along the search axis; pick the closer.
    float distance1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float distance2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    bool  isDirection1 = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeThickness = distance1 + distance2;
    float pixelOffset = -distanceFinal / edgeThickness + 0.5;

    // Guard: only shift if the luma at the near edge-end varies as expected.
    bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;
    bool correctVariation =
        ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Sub-pixel aliasing: low-pass the 3x3 luma, weighted by SUBPIX.
    float lumaAverage = (1.0 / 12.0) *
        (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
    float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
    float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
    float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * FXAA_SUBPIX;

    finalOffset = max(finalOffset, subPixelOffsetFinal);

    // Re-sample at the shifted position.
    vec2 finalUv = uv;
    if (isHorizontal) finalUv.y += finalOffset * stepLength;
    else              finalUv.x += finalOffset * stepLength;

    fragColor = vec4(texture(u_colorTexture, finalUv).rgb, 1.0);
}
