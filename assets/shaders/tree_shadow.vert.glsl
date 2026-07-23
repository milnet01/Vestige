// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_shadow.vert.glsl
/// @brief Tree shadow-caster vertex shader — same instanced transform + wind
///        sway as tree_mesh.vert, projected into light space so cast geometry
///        matches the drawn mesh exactly (design §4.4/T4, 3D_E-0033).
#version 450 core

// Loaded-mesh vertex attributes (Mesh::upload layout: 0=pos,1=normal,3=texCoord)
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_texCoord;

// Per-instance: model mat4 @6-9 (binding 1) + signed crossfade-dissolve alpha @12
// (binding 3). T9: the depth pass dissolves in lockstep with the visible canopy.
layout(location = 6) in mat4 i_model;
layout(location = 12) in float i_alpha;

uniform mat4 u_lightSpaceMatrix;
uniform mat4 u_nodeMatrix;      // baked node world transform for this primitive
uniform float u_time;
uniform vec3 u_windDirection;   // world XZ
uniform float u_windAmplitude;
uniform float u_windFrequency;

out vec2 v_texCoord;
out vec3 v_worldNormal;
out float v_alpha;

void main()
{
    // Node transform → tree-local, then per-instance transform → world.
    // MUST match tree_mesh.vert so shadows track the canopy sway (no detach).
    vec4 world = i_model * (u_nodeMatrix * vec4(a_position, 1.0));

    float baseY = i_model[3][1];
    float h = max(world.y - baseY, 0.0);
    float windPhase = u_time * u_windFrequency + world.x * 0.1 + world.z * 0.1;
    float sway = sin(windPhase) * u_windAmplitude * h * 0.05;
    world.xz += u_windDirection.xz * sway;

    mat3 nm = mat3(i_model) * mat3(u_nodeMatrix);
    v_worldNormal = normalize(nm * a_normal);
    v_texCoord = a_texCoord;
    v_alpha = i_alpha;

    gl_Position = u_lightSpaceMatrix * world;
}
