// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file debug_line.vert.glsl
/// @brief Debug line vertex shader — transforms colored line vertices for gizmo and debug visualization.
#version 450 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;

uniform mat4 u_viewProjection;

out vec3 v_color;

void main()
{
    gl_Position = u_viewProjection * vec4(a_position, 1.0);
    v_color = a_color;
}
