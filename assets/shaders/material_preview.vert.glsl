// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_preview.vert.glsl
/// @brief Material preview vertex shader — transforms preview sphere geometry with cofactor normal matrix.
#version 450 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texCoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_texCoord;

/// Computes the cofactor matrix for correct non-uniform scale normal transform.
mat3 cofactorMatrix(mat3 m)
{
    return mat3(cross(m[1], m[2]),
                cross(m[2], m[0]),
                cross(m[0], m[1]));
}

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    v_normal = normalize(cofactorMatrix(mat3(u_model)) * a_normal);
    v_texCoord = a_texCoord;
    gl_Position = u_projection * u_view * worldPos;
}
