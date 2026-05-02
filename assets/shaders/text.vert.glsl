// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file text.vert.glsl
/// @brief Text rendering vertex shader — transforms glyph quads with packed
///        position, UV and per-vertex RGB color (Phase 10.9 Pe1 batched path).
#version 450 core

layout(location = 0) in vec4 vertex;  // xy = position, zw = texcoord
layout(location = 1) in vec3 a_color; // per-vertex RGB so a single batch can mix HUD colors

uniform mat4 u_projection;
uniform mat4 u_model;

out vec2 v_texCoord;
out vec3 v_color;

void main()
{
    gl_Position = u_projection * u_model * vec4(vertex.xy, 0.0, 1.0);
    v_texCoord = vertex.zw;
    v_color = a_color;
}
