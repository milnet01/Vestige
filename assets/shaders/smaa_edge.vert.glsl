// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Contains code copied verbatim from SMAA (https://github.com/iryoku/smaa,
// commit 71c806a8), Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria,
// Belen Masia, Fernando Navarro and Diego Gutierrez, MIT licence. The full
// licence text is external/smaa/LICENSE.txt.

/// @file smaa_edge.vert.glsl
/// @brief SMAA pass 1 vertex shader: neighbour offsets for luma edge detection.
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

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;

out vec2 v_texCoord;
out vec4 v_offset[3];

// ---- BEGIN verbatim external/smaa/SMAA.hlsl ----
void SMAAEdgeDetectionVS(float2 texcoord,
                         out float4 offset[3]) {
    offset[0] = mad(SMAA_RT_METRICS.xyxy, float4(-1.0, 0.0, 0.0, -1.0), texcoord.xyxy);
    offset[1] = mad(SMAA_RT_METRICS.xyxy, float4( 1.0, 0.0, 0.0,  1.0), texcoord.xyxy);
    offset[2] = mad(SMAA_RT_METRICS.xyxy, float4(-2.0, 0.0, 0.0, -2.0), texcoord.xyxy);
}
// ---- END verbatim external/smaa/SMAA.hlsl ----

void main()
{
    v_texCoord = a_texCoord;
    SMAAEdgeDetectionVS(a_texCoord, v_offset);
    gl_Position = vec4(a_position, 0.0, 1.0);
}
