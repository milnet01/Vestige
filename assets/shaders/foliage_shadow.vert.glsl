// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file foliage_shadow.vert.glsl
/// @brief Foliage shadow depth vertex shader — same wind animation as main pass.
#version 450 core

// Star mesh vertex attributes
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texCoord;

// Per-instance attributes
layout(location = 3) in vec3 i_position;
layout(location = 4) in float i_rotation;
layout(location = 5) in float i_scale;

uniform mat4 u_lightSpaceMatrix;
uniform float u_time;
uniform vec3 u_windDirection;
uniform float u_windAmplitude;
uniform float u_windFrequency;

out vec2 v_texCoord;

void main()
{
    // Apply per-instance Y-axis rotation (must match main pass exactly)
    float s = sin(i_rotation);
    float c = cos(i_rotation);
    vec3 rotated = vec3(
        a_position.x * c - a_position.z * s,
        a_position.y,
        a_position.x * s + a_position.z * c
    );

    // Apply scale and world position
    vec3 worldPos = rotated * i_scale + i_position;

    // Wind animation (must match main pass for consistent shadow geometry)
    float heightFactor = a_position.y * 2.5;
    float windPhase = u_time * u_windFrequency
                    + i_position.x * 0.5
                    + i_position.z * 0.3;
    float windOffset = sin(windPhase) * u_windAmplitude * heightFactor;
    worldPos.xz += u_windDirection.xz * windOffset;

    gl_Position = u_lightSpaceMatrix * vec4(worldPos, 1.0);
    v_texCoord = a_texCoord;
}
