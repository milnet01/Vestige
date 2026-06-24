// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file point_shadow_depth.vert.glsl
/// @brief Point light shadow cubemap vertex shader — transforms geometry into light space with instancing support.
#version 450 core

layout(location = 0) in vec3 position;
// Phase 13 G1: normal + UV feed the RSM flux term (albedo·radiance·N·L·atten).
layout(location = 1) in vec3 normal;
layout(location = 3) in vec2 texCoord;

// Per-instance model matrix (locations 6-9)
layout(location = 6) in vec4 instanceModelCol0;
layout(location = 7) in vec4 instanceModelCol1;
layout(location = 8) in vec4 instanceModelCol2;
layout(location = 9) in vec4 instanceModelCol3;

uniform mat4 u_model;
uniform mat4 u_lightSpaceMatrix;
uniform bool u_useInstancing;

out vec3 v_fragPosition;
out vec3 v_worldNormal;
out vec2 v_texCoord;

void main()
{
    mat4 model;
    if (u_useInstancing)
    {
        model = mat4(instanceModelCol0, instanceModelCol1,
                     instanceModelCol2, instanceModelCol3);
    }
    else
    {
        model = u_model;
    }

    vec4 worldPos = model * vec4(position, 1.0);
    v_fragPosition = vec3(worldPos);
    // mat3(model) normal transform — see shadow_depth.vert.glsl for rationale.
    v_worldNormal = mat3(model) * normal;
    v_texCoord = texCoord;
    gl_Position = u_lightSpaceMatrix * worldPos;
}
