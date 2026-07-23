// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_mesh.vert.glsl
/// @brief Tree mesh LOD vertex shader — instanced artist mesh with per-node
///        matrix, wind sway, and CSM view depth. (design §4.4/§4.6, 3D_E-0033)
#version 450 core

// Loaded-mesh vertex attributes (Mesh::upload layout: 0=pos,1=normal,3=texCoord,
// 4=tangent,5=bitangent). Tangent/bitangent feed the normal-mapping TBN (T8).
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_texCoord;
layout(location = 4) in vec3 a_tangent;
layout(location = 5) in vec3 a_bitangent;

// Per-instance: model mat4 @6-9 (binding 1), crossfade alpha @12 (binding 3)
layout(location = 6) in mat4 i_model;
layout(location = 12) in float i_alpha;

uniform mat4 u_viewProjection;
uniform mat4 u_view;
uniform mat4 u_nodeMatrix;      // baked node world transform for this primitive
uniform float u_time;
uniform vec3 u_windDirection;   // world XZ
uniform float u_windAmplitude;
uniform float u_windFrequency;
uniform vec4 u_clipPlane;       // water clip plane (0 = disabled)

out vec3 v_normal;
out vec3 v_tangent;
out vec3 v_bitangent;
out vec3 v_worldPos;
out vec2 v_texCoord;
out float v_alpha;
out float v_viewDepth;

// Copied from scene.vert (no shared GLSL #include in this engine): normalize
// unless the vector collapses, then fall back to a world axis so a degenerate
// synthesized tangent can't produce NaN lighting.
vec3 safeNormalize(vec3 v, vec3 fallback)
{
    float lenSq = dot(v, v);
    return (lenSq > 1e-12) ? (v * inversesqrt(lenSq)) : fallback;
}

void main()
{
    // Node transform → tree-local, then per-instance transform → world.
    vec4 world = i_model * (u_nodeMatrix * vec4(a_position, 1.0));

    // Canopy wind sway: gentle, scaled by height above the trunk base, calm at
    // wind 0. Phase varies spatially so neighbours don't sway in lockstep.
    float baseY = i_model[3][1];
    float h = max(world.y - baseY, 0.0);
    float windPhase = u_time * u_windFrequency + world.x * 0.1 + world.z * 0.1;
    float sway = sin(windPhase) * u_windAmplitude * h * 0.05;
    world.xz += u_windDirection.xz * sway;

    mat3 nm = mat3(i_model) * mat3(u_nodeMatrix);
    v_normal = normalize(nm * a_normal);
    // World-space TBN vectors (independent normalize + world-axis fallback,
    // mirroring scene.vert). Perturbed normal is assembled in the fragment.
    v_tangent = safeNormalize(nm * a_tangent, vec3(1.0, 0.0, 0.0));
    v_bitangent = safeNormalize(nm * a_bitangent, vec3(0.0, 1.0, 0.0));
    v_worldPos = world.xyz;
    v_texCoord = a_texCoord;
    v_alpha = i_alpha;
    v_viewDepth = -(u_view * world).z;

    gl_Position = u_viewProjection * world;
    gl_ClipDistance[0] = dot(world, u_clipPlane);
}
