// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file smaa_neighborhood.vert.glsl
/// @brief SMAA neighborhood blending vertex shader — precomputes neighbor offset.
#version 450 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;

uniform vec4 u_rtMetrics; // (1/width, 1/height, width, height)

out vec2 v_texCoord;
out vec4 v_offset;  // Right and bottom neighbor coordinates

void main()
{
    v_texCoord = a_texCoord;
    v_offset = u_rtMetrics.xyxy * vec4(1.0, 0.0, 0.0, 1.0) + a_texCoord.xyxy;

    gl_Position = vec4(a_position, 0.0, 1.0);
}
