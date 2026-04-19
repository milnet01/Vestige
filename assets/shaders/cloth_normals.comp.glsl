// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_normals.comp.glsl
/// @brief Phase 9B Step 8 — per-vertex normals for grid-topology cloth.
///
/// One thread per particle. Walks the (up to) 6 triangles of the regular
/// grid that touch this vertex, accumulates area-weighted face normals,
/// then normalises. Atomic-free: each particle's normal is the unique
/// writer of its own slot, and reads are read-only.
///
/// This grid-walking approach is correct only for the intact regular
/// triangulation produced by `GpuClothSimulator::buildInitialGrid()`.
/// Once tearing or non-grid topology lands (deferred), this shader needs
/// replacement with a generic two-pass approach (per-triangle face
/// normals → per-vertex sum via adjacency table).

#version 450 core

layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly  buffer Positions { vec4 positions[]; };
layout(std430, binding = 6) writeonly buffer Normals   { vec4 normals[];   };

uniform uint u_particleCount;
uniform uint u_gridW;
uniform uint u_gridH;

vec3 getPos(uint idx) { return positions[idx].xyz; }

// Cross-product order matches the index winding in `buildInitialGrid`:
// tri = (a, b, c) with cross(b-a, c-a). Result is area-weighted (length
// proportional to the triangle area), which is the desired weighting for
// per-vertex normal accumulation.
vec3 faceNormal(uint a, uint b, uint c)
{
    vec3 pa = getPos(a);
    vec3 pb = getPos(b);
    vec3 pc = getPos(c);
    return cross(pb - pa, pc - pa);
}

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    uint x = id % u_gridW;
    uint z = id / u_gridW;

    vec3 acc = vec3(0.0);

    // Cell (x, z) — vertex P = i0 of this cell. Triangle A = (i0, i2, i1).
    if (x + 1 < u_gridW && z + 1 < u_gridH)
    {
        uint i0 = id;
        uint i1 = id + 1;
        uint i2 = id + u_gridW;
        acc += faceNormal(i0, i2, i1);
    }

    // Cell (x-1, z) — vertex P = i1. Touches both triangles A and B.
    if (x > 0 && z + 1 < u_gridH)
    {
        uint i0 = id - 1;
        uint i1 = id;
        uint i2 = id + u_gridW - 1;
        uint i3 = id + u_gridW;
        acc += faceNormal(i0, i2, i1);
        acc += faceNormal(i1, i2, i3);
    }

    // Cell (x, z-1) — vertex P = i2. Touches both triangles A and B.
    if (z > 0 && x + 1 < u_gridW)
    {
        uint i0 = id - u_gridW;
        uint i1 = id - u_gridW + 1;
        uint i2 = id;
        uint i3 = id + 1;
        acc += faceNormal(i0, i2, i1);
        acc += faceNormal(i1, i2, i3);
    }

    // Cell (x-1, z-1) — vertex P = i3. Triangle B = (i1, i2, i3) only.
    if (x > 0 && z > 0)
    {
        uint i1 = id - u_gridW;
        uint i2 = id - 1;
        uint i3 = id;
        acc += faceNormal(i1, i2, i3);
    }

    float len = length(acc);
    vec3 n = (len > 1e-7) ? (acc / len) : vec3(0.0, 1.0, 0.0);
    normals[id].xyz = n;
}
