// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Contains code copied verbatim from SMAA (https://github.com/iryoku/smaa,
// commit 71c806a8), Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria,
// Belen Masia, Fernando Navarro and Diego Gutierrez, MIT licence. The full
// licence text is external/smaa/LICENSE.txt.

/// @file smaa_neighborhood.frag.glsl
/// @brief SMAA pass 3: blends each pixel with its neighbours using the pass 2 weights.
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

uniform sampler2D u_colorTexture; // HDR scene, bilinear
uniform sampler2D u_blendTexture; // pass 2 output

in vec2 v_texCoord;
in vec4 v_offset;

out vec4 fragColor;

// ---- BEGIN verbatim external/smaa/SMAA.hlsl ----
void SMAAMovc(bool2 cond, inout float2 variable, float2 value) {
    SMAA_FLATTEN if (cond.x) variable.x = value.x;
    SMAA_FLATTEN if (cond.y) variable.y = value.y;
}

void SMAAMovc(bool4 cond, inout float4 variable, float4 value) {
    SMAAMovc(cond.xy, variable.xy, value.xy);
    SMAAMovc(cond.zw, variable.zw, value.zw);
}

float4 SMAANeighborhoodBlendingPS(float2 texcoord,
                                  float4 offset,
                                  SMAATexture2D(colorTex),
                                  SMAATexture2D(blendTex)
                                  #if SMAA_REPROJECTION
                                  , SMAATexture2D(velocityTex)
                                  #endif
                                  ) {
    // Fetch the blending weights for current pixel:
    float4 a;
    a.x = SMAASample(blendTex, offset.xy).a; // Right
    a.y = SMAASample(blendTex, offset.zw).g; // Top
    a.wz = SMAASample(blendTex, texcoord).xz; // Bottom / Left

    // Is there any blending weight with a value greater than 0.0?
    SMAA_BRANCH
    if (dot(a, float4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
        float4 color = SMAASampleLevelZero(colorTex, texcoord);

        #if SMAA_REPROJECTION
        float2 velocity = SMAA_DECODE_VELOCITY(SMAASampleLevelZero(velocityTex, texcoord));

        // Pack velocity into the alpha channel:
        color.a = sqrt(5.0 * length(velocity));
        #endif

        return color;
    } else {
        bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)

        // Calculate the blending offsets:
        float4 blendingOffset = float4(0.0, a.y, 0.0, a.w);
        float2 blendingWeight = a.yw;
        SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0));
        SMAAMovc(bool2(h, h), blendingWeight, a.xz);
        blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

        // Calculate the texture coordinates:
        float4 blendingCoord = mad(blendingOffset, float4(SMAA_RT_METRICS.xy, -SMAA_RT_METRICS.xy), texcoord.xyxy);

        // We exploit bilinear filtering to mix current pixel with the chosen
        // neighbor:
        float4 color = blendingWeight.x * SMAASampleLevelZero(colorTex, blendingCoord.xy);
        color += blendingWeight.y * SMAASampleLevelZero(colorTex, blendingCoord.zw);

        #if SMAA_REPROJECTION
        // Antialias velocity for proper reprojection in a later stage:
        float2 velocity = blendingWeight.x * SMAA_DECODE_VELOCITY(SMAASampleLevelZero(velocityTex, blendingCoord.xy));
        velocity += blendingWeight.y * SMAA_DECODE_VELOCITY(SMAASampleLevelZero(velocityTex, blendingCoord.zw));

        // Pack velocity into the alpha channel:
        color.a = sqrt(5.0 * length(velocity));
        #endif

        return color;
    }
}
// ---- END verbatim external/smaa/SMAA.hlsl ----

void main()
{
    fragColor = SMAANeighborhoodBlendingPS(v_texCoord, v_offset, u_colorTexture, u_blendTexture);
}
