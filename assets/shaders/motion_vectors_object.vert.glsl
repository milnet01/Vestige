// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_vectors_object.vert.glsl
/// @brief Per-object motion vector vertex shader.
///
/// AUDIT.md §H15 / FIXPLAN G1. Projects each vertex through both the current
/// and previous frame's view-projection * model matrices, then the fragment
/// shader writes `currentClip.xy/w - previousClip.xy/w` scaled to UV space.
/// This gives correct motion for dynamic / animated objects that the prior
/// full-screen depth-reprojection pass could only represent as camera-motion.
#version 450 core

layout(location = 0) in vec3 a_position;

uniform mat4 u_model;
uniform mat4 u_prevModel;
uniform mat4 u_viewProjection;
uniform mat4 u_prevViewProjection;

out vec4 v_currentClip;
out vec4 v_prevClip;

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    vec4 prevWorldPos = u_prevModel * vec4(a_position, 1.0);

    v_currentClip = u_viewProjection * worldPos;
    v_prevClip = u_prevViewProjection * prevWorldPos;

    // Rasterise at the current frame position — depth test discards pixels
    // already covered by nearer geometry.
    gl_Position = v_currentClip;
}
