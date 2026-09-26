// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Contains code copied verbatim from SMAA (https://github.com/iryoku/smaa,
// commit 71c806a8), Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria,
// Belen Masia, Fernando Navarro and Diego Gutierrez, MIT licence. The full
// licence text is external/smaa/LICENSE.txt.

/// @file smaa_edge.frag.glsl
/// @brief SMAA pass 1: luma edge detection with local contrast adaptation.
///
/// The functions between the BEGIN/END markers are the reference SMAA 1x
/// code, HIGH preset, unchanged. tests/test_smaa.cpp compares each one with
/// external/smaa/SMAA.hlsl, so an edit here fails the build's tests.
///
/// Orientation: SMAA was written for Direct3D, where texture row 0 is the
/// top of the image. Every SMAA pass here reads and writes in texture-row
/// order, and the lookup tables are uploaded row 0 first, so the algorithm
/// sees the image mirrored vertically and stays self-consistent. No Y flip
/// is needed anywhere.
///
/// Vestige difference: the reference requires gamma-corrected input for luma
/// edge detection, and Vestige's input is linear HDR. The reference leaves
/// its sampling macros as the porting point, so SMAASamplePoint is redefined
/// below to compress each sample to [0,1] (x / (1 + x)) and gamma-encode it
/// (1 / 2.2) before the luma is taken. The function body is untouched.
/// Only this pass sees compressed values: pass 3 blends the linear HDR
/// scene, so nothing needs to be un-compressed afterwards.
#version 450 core

// ---------------------------------------------------------------------------
// SMAA configuration, copied verbatim from external/smaa/SMAA.hlsl: the
// GLSL porting layer, the HIGH preset values and the non-configurable table
// constants. Two defines are Vestige's own: SMAA_GLSL_4 selects the porting
// layer, and SMAA_RT_METRICS names the uniform. tests/test_smaa.cpp pins
// the rest.
// ---------------------------------------------------------------------------
#define SMAA_GLSL_4
#define SMAA_THRESHOLD 0.1
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_CORNER_ROUNDING 25
#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0
#define SMAA_PREDICATION 0
#define SMAA_REPROJECTION 0
#define SMAA_AREATEX_SELECT(sample) sample.rg
#define SMAA_SEARCHTEX_SELECT(sample) sample.r
#define SMAA_AREATEX_MAX_DISTANCE 16
#define SMAA_AREATEX_MAX_DISTANCE_DIAG 20
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / float2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE float2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE float2(64.0, 16.0)
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)
#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASampleLevelZeroPoint(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) textureLodOffset(tex, coord, 0.0, offset)
#define SMAASample(tex, coord) texture(tex, coord)
#define SMAASamplePoint(tex, coord) texture(tex, coord)
#define SMAASampleOffset(tex, coord, offset) texture(tex, coord, offset)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define lerp(a, b, t) mix(a, b, t)
#define saturate(a) clamp(a, 0.0, 1.0)
#define mad(a, b, c) fma(a, b, c)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4

uniform vec4 u_rtMetrics; // (1/width, 1/height, width, height)
#define SMAA_RT_METRICS u_rtMetrics

uniform sampler2D u_colorTexture;

in vec2 v_texCoord;
in vec4 v_offset[3];

out vec4 fragColor;

/// Linear HDR -> display-like [0,1] value for the luma threshold.
vec4 vestigeSmaaEdgeSample(sampler2D tex, vec2 coord)
{
    vec3 c = max(textureLod(tex, coord, 0.0).rgb, vec3(0.0));
    return vec4(pow(c / (vec3(1.0) + c), vec3(1.0 / 2.2)), 1.0);
}
#undef SMAASamplePoint
#define SMAASamplePoint(tex, coord) vestigeSmaaEdgeSample(tex, coord)

// ---- BEGIN verbatim external/smaa/SMAA.hlsl ----
float2 SMAALumaEdgeDetectionPS(float2 texcoord,
                               float4 offset[3],
                               SMAATexture2D(colorTex)
                               #if SMAA_PREDICATION
                               , SMAATexture2D(predicationTex)
                               #endif
                               ) {
    // Calculate the threshold:
    #if SMAA_PREDICATION
    float2 threshold = SMAACalculatePredicatedThreshold(texcoord, offset, SMAATexturePass2D(predicationTex));
    #else
    float2 threshold = float2(SMAA_THRESHOLD, SMAA_THRESHOLD);
    #endif

    // Calculate lumas:
    float3 weights = float3(0.2126, 0.7152, 0.0722);
    float L = dot(SMAASamplePoint(colorTex, texcoord).rgb, weights);

    float Lleft = dot(SMAASamplePoint(colorTex, offset[0].xy).rgb, weights);
    float Ltop  = dot(SMAASamplePoint(colorTex, offset[0].zw).rgb, weights);

    // We do the usual threshold:
    float4 delta;
    delta.xy = abs(L - float2(Lleft, Ltop));
    float2 edges = step(threshold, delta.xy);

    // Then discard if there is no edge:
    if (dot(edges, float2(1.0, 1.0)) == 0.0)
        discard;

    // Calculate right and bottom deltas:
    float Lright = dot(SMAASamplePoint(colorTex, offset[1].xy).rgb, weights);
    float Lbottom  = dot(SMAASamplePoint(colorTex, offset[1].zw).rgb, weights);
    delta.zw = abs(L - float2(Lright, Lbottom));

    // Calculate the maximum delta in the direct neighborhood:
    float2 maxDelta = max(delta.xy, delta.zw);

    // Calculate left-left and top-top deltas:
    float Lleftleft = dot(SMAASamplePoint(colorTex, offset[2].xy).rgb, weights);
    float Ltoptop = dot(SMAASamplePoint(colorTex, offset[2].zw).rgb, weights);
    delta.zw = abs(float2(Lleft, Ltop) - float2(Lleftleft, Ltoptop));

    // Calculate the final maximum delta:
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    // Local contrast adaptation:
    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    return edges;
}
// ---- END verbatim external/smaa/SMAA.hlsl ----

void main()
{
    fragColor = vec4(SMAALumaEdgeDetectionPS(v_texCoord, v_offset, u_colorTexture), 0.0, 0.0);
}
