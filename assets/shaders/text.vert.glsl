// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file text.vert.glsl
/// @brief Text rendering vertex shader — transforms glyph quads with packed position and UV data.
#version 450 core

layout(location = 0) in vec4 vertex;  // xy = position, zw = texcoord

uniform mat4 u_projection;
uniform mat4 u_model;

out vec2 v_texCoord;

void main()
{
    gl_Position = u_projection * u_model * vec4(vertex.xy, 0.0, 1.0);
    v_texCoord = vertex.zw;
}
