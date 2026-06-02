// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_wind_drag.comp.glsl
/// @brief Phase 10.9 Sh4a — per-triangle aerodynamic drag (APPROXIMATE tier).
///
/// One thread per triangle within a colour group. Within a colour, no two
/// triangles share any vertex (CPU-side `colourTriangleConstraints` guarantees
/// this), so each thread writes per-vertex velocity deltas directly without
/// atomics — and across GPU runs the colour-dispatch order is deterministic.
///
/// Math matches the CPU `ClothSimulator::applyWind` APPROXIMATE path
/// (`cloth_simulator.cpp:2008-2052`) with `windVel = baseWindVel` (no
/// per-triangle turbulence; that is the Sh4b FULL tier).
///
/// NOTE — runtime parity caveat: `u_windVelocity` here is the bare
/// direction*strength vector. The CPU `baseWindVel` additionally folds in
/// `gustCurrent`, `cachedFlutter`, and `windDirOffset`; folding those into the
/// uploaded wind velocity is deferred to Sh4b, so when gusts/flutter are
/// active the two paths diverge until then. The colour ordering also assumes
/// non-degenerate triangles with three distinct vertex indices (the regular
/// cloth grid always satisfies this); a triangle with a repeated index would
/// alias its per-vertex writes within the thread.
///
/// Per-particle math:
///
///   vAvg      = (v[i0] + v[i1] + v[i2]) / 3
///   vRel      = windVel - vAvg
///   edge1     = p[i1] - p[i0]
///   edge2     = p[i2] - p[i0]
///   crossVec  = cross(edge1, edge2)
///   area2     = length(crossVec)
///   normal    = crossVec / area2
///   area      = area2 * 0.5
///   force     = normal * (0.5 * dragCoeff * area * dot(vRel, normal))
///   pv        = force * (dt / 3)
///   v[iN]    += pv * invMass[iN]     (skipped for pinned particles: invMass==0)
///
/// SSBO bindings match `GpuClothSimulator::BufferBinding`:
///   binding 0 — Positions (read-only for normal + area; vec4, w = invMass)
///   binding 2 — Velocities (read+write; vec4, w unused)
///   binding 9 — Triangles  (read-only; uvec4, xyz = i0/i1/i2, w = pad)

#version 450 core

layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly buffer Positions
{
    vec4 positions[];  // xyz = world pos, w = invMass
};

layout(std430, binding = 2) buffer Velocities
{
    vec4 velocities[];  // xyz = velocity, w unused
};

struct Triangle
{
    uvec4 idx;  // x=i0, y=i1, z=i2, w=pad
};

layout(std430, binding = 9) readonly buffer Triangles
{
    Triangle triangles[];
};

uniform uint  u_firstTri;     ///< Offset into triangles[] for this colour group.
uniform uint  u_triCount;     ///< Number of triangles in this colour group.
uniform vec3  u_windVelocity; ///< Uniform wind vector (direction * strength).
uniform float u_dragCoeff;    ///< Aerodynamic drag coefficient (matches CPU m_dragCoeff).
uniform float u_deltaTime;    ///< Substep dt.

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_triCount) return;

    Triangle tri = triangles[u_firstTri + id];
    uint i0 = tri.idx.x;
    uint i1 = tri.idx.y;
    uint i2 = tri.idx.z;

    vec3 p0 = positions[i0].xyz;
    vec3 p1 = positions[i1].xyz;
    vec3 p2 = positions[i2].xyz;

    vec3 v0 = velocities[i0].xyz;
    vec3 v1 = velocities[i1].xyz;
    vec3 v2 = velocities[i2].xyz;

    // Per-triangle aerodynamic drag: relative wind vs average triangle velocity.
    vec3 vAvg = (v0 + v1 + v2) / 3.0;
    vec3 vRel = u_windVelocity - vAvg;

    vec3 edge1    = p1 - p0;
    vec3 edge2    = p2 - p0;
    vec3 crossVec = cross(edge1, edge2);
    float area2   = length(crossVec);
    if (area2 < 1e-7) return;  // Degenerate triangle — skip.

    vec3  normal = crossVec / area2;
    float area   = area2 * 0.5;

    float vDotN = dot(vRel, normal);
    vec3  force = normal * (0.5 * u_dragCoeff * area * vDotN);
    vec3  pv    = force * (u_deltaTime / 3.0);

    // Apply per-vertex share, respecting pin state (invMass == 0 means pinned).
    float w0 = positions[i0].w;
    float w1 = positions[i1].w;
    float w2 = positions[i2].w;

    if (w0 > 0.0) velocities[i0].xyz = v0 + pv * w0;
    if (w1 > 0.0) velocities[i1].xyz = v1 + pv * w1;
    if (w2 > 0.0) velocities[i2].xyz = v2 + pv * w2;
}
