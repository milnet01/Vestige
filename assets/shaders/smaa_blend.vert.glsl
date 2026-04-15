// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file smaa_blend.vert.glsl
/// @brief SMAA blend weight calculation vertex shader — precomputes search offsets.
#version 450 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;

uniform vec4 u_rtMetrics; // (1/width, 1/height, width, height)

out vec2 v_texCoord;
out vec2 v_pixCoord;       // Texture coordinate in pixel units
out vec4 v_offset[3];      // Precomputed search offsets

const int SMAA_MAX_SEARCH_STEPS = 16;

void main()
{
    v_texCoord = a_texCoord;
    v_pixCoord = a_texCoord * u_rtMetrics.zw;  // Convert to pixel coordinates

    // Offsets for blend weight calculation (fractional values for bilinear disambiguation)
    v_offset[0] = u_rtMetrics.xyxy * vec4(-0.25, -0.125,  1.25, -0.125) + a_texCoord.xyxy;
    v_offset[1] = u_rtMetrics.xyxy * vec4(-0.125, -0.25, -0.125,  1.25) + a_texCoord.xyxy;

    // Extended search range offsets
    v_offset[2] = u_rtMetrics.xxyy * vec4(-2.0, 2.0, -2.0, 2.0)
                  * float(SMAA_MAX_SEARCH_STEPS)
                  + vec4(v_offset[0].xz, v_offset[1].yw);

    gl_Position = vec4(a_position, 0.0, 1.0);
}
