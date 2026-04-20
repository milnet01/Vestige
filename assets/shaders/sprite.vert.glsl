// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#version 450 core

// Per-vertex (corner of the unit quad, 0..1 on both axes).
layout (location = 0) in vec2 a_corner;

// Per-instance — see SpriteInstance in sprite_renderer.h for layout.
// The 2D affine transform is packed into two vec4 rows:
//   row0 = (a, b, tx, _unused)
//   row1 = (c, d, ty, _unused)
// implying third row (0, 0, 1). This skips 10 wasted floats per instance
// vs storing a full mat4.
layout (location = 1) in vec4 a_transformRow0;
layout (location = 2) in vec4 a_transformRow1;
layout (location = 3) in vec4 a_uvRect;   // (u0, v0, u1, v1)
layout (location = 4) in vec4 a_tint;
layout (location = 5) in float a_depth;   // [0..1], written to gl_Position.z

uniform mat4 u_viewProj;

out vec2 v_uv;
out vec4 v_tint;

void main()
{
    // Reconstruct the 3x3 affine matrix from its two rows and the implicit
    // third row (0, 0, 1). Multiply against the centred corner in homogeneous
    // coords so pivot maths lands at the origin of the matrix.
    vec3 local = vec3(a_corner - 0.5, 1.0);
    vec3 world = vec3(
        dot(a_transformRow0.xyz, local),
        dot(a_transformRow1.xyz, local),
        0.0);

    // Write the sort-key-derived depth so the 3D depth buffer orders sprites
    // correctly against it. The depth range is remapped CPU-side into
    // [0.99, 0.999] so sprites always sit in front of cleared depth.
    gl_Position = u_viewProj * vec4(world.xy, 0.0, 1.0);
    gl_Position.z = a_depth * 2.0 - 1.0;  // [0,1] → [-1,1] clip-space z

    v_uv = mix(a_uvRect.xy, a_uvRect.zw, a_corner);
    v_tint = a_tint;
}
