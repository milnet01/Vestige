// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle.vert.glsl
/// @brief Billboard particle vertex shader — camera-facing quads via instancing.
#version 450 core

// Per-vertex: static quad local offset (-0.5 to 0.5)
layout(location = 0) in vec2 a_quadPos;

// Per-instance
layout(location = 1) in vec3 a_worldPos;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_size;
layout(location = 4) in float a_normalizedAge;

uniform mat4 u_viewProjection;
uniform vec3 u_cameraRight;
uniform vec3 u_cameraUp;

out vec2 v_texCoord;
out vec4 v_color;
out float v_normalizedAge;

void main()
{
    // Expand the quad in world space to face the camera
    vec3 worldPos = a_worldPos
        + u_cameraRight * a_quadPos.x * a_size
        + u_cameraUp * a_quadPos.y * a_size;

    gl_Position = u_viewProjection * vec4(worldPos, 1.0);

    // Map quad offset (-0.5..0.5) to UV (0..1)
    v_texCoord = a_quadPos + 0.5;
    v_color = a_color;
    v_normalizedAge = a_normalizedAge;
}
