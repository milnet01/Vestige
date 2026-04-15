// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file water.vert.glsl
/// @brief Water surface vertex shader — displaces vertices with Gerstner wave summation.
#version 450 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texCoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_time;

// Wave parameters: (amplitude, wavelength, speed, direction_radians)
uniform int u_numWaves;
uniform vec4 u_waveParams[4];

out vec3 v_worldPos;
out vec2 v_texCoord;
out vec4 v_clipSpace;
out vec3 v_normal;

const float PI = 3.14159265359;

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);

    // Sum-of-sines wave displacement
    float height = 0.0;
    float dHdx = 0.0;
    float dHdz = 0.0;

    for (int i = 0; i < u_numWaves; i++)
    {
        float A   = u_waveParams[i].x;  // amplitude
        float wl  = u_waveParams[i].y;  // wavelength
        float spd = u_waveParams[i].z;  // speed
        float dir = u_waveParams[i].w;  // direction in radians

        float w = 2.0 * PI / wl;        // angular frequency
        float phi = spd * w;            // phase velocity
        vec2 D = vec2(cos(dir), sin(dir));

        float proj = dot(D, worldPos.xz);
        float phase = proj * w + u_time * phi;

        height += A * sin(phase);
        dHdx += A * w * D.x * cos(phase);
        dHdz += A * w * D.y * cos(phase);
    }

    worldPos.y += height;

    // Compute wave normal from partial derivatives
    v_normal = normalize(vec3(-dHdx, 1.0, -dHdz));

    v_worldPos = vec3(worldPos);
    v_texCoord = a_texCoord;
    v_clipSpace = u_projection * u_view * worldPos;
    gl_Position = v_clipSpace;
}
