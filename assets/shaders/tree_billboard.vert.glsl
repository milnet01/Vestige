// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_billboard.vert.glsl
/// @brief Tree billboard vertex shader — yaw-billboard artist card sized from
///        per-species extents, oriented from a per-pass camera-right uniform
///        (main or pond-reflection camera). (design §4.3, 3D_E-0033)
#version 450 core

layout(location = 0) in vec2 a_offset;    // x in [-1,1], y in [0,1] (base→top)
layout(location = 1) in vec2 a_texCoord;

// Per-instance
layout(location = 3) in vec3 i_position;   // trunk-base world position
layout(location = 4) in float i_scale;
layout(location = 5) in float i_alpha;
layout(location = 6) in float i_halfWidth; // species card half-width (m)
layout(location = 7) in float i_height;    // species card height (m)

uniform mat4 u_viewProjection;
uniform mat4 u_view;
uniform vec3 u_cameraRight;
uniform vec3 u_cameraUp;
uniform vec4 u_clipPlane;     // water clip plane (0 = disabled)

out vec2 v_texCoord;
out float v_alpha;
out vec3 v_worldPos;
out float v_viewDepth;

void main()
{
    float hw = i_halfWidth * i_scale;
    float ht = i_height * i_scale;

    vec3 world = i_position
        + u_cameraRight * a_offset.x * hw
        + u_cameraUp * a_offset.y * ht;

    vec4 worldPosition = vec4(world, 1.0);
    gl_Position = u_viewProjection * worldPosition;
    gl_ClipDistance[0] = dot(worldPosition, u_clipPlane);

    v_texCoord = a_texCoord;
    v_alpha = i_alpha;
    v_worldPos = world;
    v_viewDepth = -(u_view * worldPosition).z;
}
