// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_dihedral.comp.glsl
/// @brief Phase 9B Step 6 — XPBD dihedral bending constraint solver.
///
/// One thread per dihedral constraint within a colour group. A dihedral
/// constraint binds two adjacent triangles (sharing an edge p2–p3) and
/// keeps the angle between their face normals near a rest angle. Math
/// follows Müller et al. 2007 — the same formulation `ClothSimulator`
/// uses on the CPU path so behaviour matches.
///
/// Within a colour, no two constraints touch any of the same four particles
/// (CPU-side `colourDihedralConstraints` guarantees this), so writes are
/// race-free without atomics.
///
/// SSBO bindings:
///   binding 0 — Positions (read+write, vec4 xyz + invMass)
///   binding 5 — DihedralConstraints (read-only)

#version 450 core

layout(local_size_x = 32) in;

struct DihedralConstraint
{
    uvec4 p;        // p0=wing0, p1=wing1, p2=edge0, p3=edge1
    vec4  params;   // x=restAngle, y=compliance, zw=padding
};

layout(std430, binding = 0) buffer Positions
{
    vec4 positions[];
};

layout(std430, binding = 5) readonly buffer DihedralConstraints
{
    DihedralConstraint dihedrals[];
};

uniform uint  u_colorOffset;     // First dihedral index for this colour group.
uniform uint  u_colorCount;      // Dihedrals in this colour group.
uniform float u_dtSubSquared;    // dt² of the current substep.

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_colorCount) return;

    DihedralConstraint c = dihedrals[u_colorOffset + id];
    uint i0 = c.p.x;
    uint i1 = c.p.y;
    uint i2 = c.p.z;
    uint i3 = c.p.w;
    float restAngle  = c.params.x;
    float compliance = c.params.y;

    vec4 p0v = positions[i0];
    vec4 p1v = positions[i1];
    vec4 p2v = positions[i2];
    vec4 p3v = positions[i3];

    float w0 = p0v.w;
    float w1 = p1v.w;
    float w2 = p2v.w;
    float w3 = p3v.w;
    if (w0 + w1 + w2 + w3 == 0.0) return;  // All four pinned.

    vec3 p0 = p0v.xyz;
    vec3 p1 = p1v.xyz;
    vec3 p2 = p2v.xyz;
    vec3 p3 = p3v.xyz;

    vec3 n1 = cross(p2 - p0, p3 - p0);
    vec3 n2 = cross(p3 - p1, p2 - p1);
    float len1 = length(n1);
    float len2 = length(n2);
    if (len1 < 1e-7 || len2 < 1e-7) return;
    n1 /= len1;
    n2 /= len2;

    float cosAngle = clamp(dot(n1, n2), -1.0, 1.0);
    float phi      = acos(cosAngle);
    float C        = phi - restAngle;
    if (abs(C) < 1e-7) return;

    vec3 e = p3 - p2;
    float elen = length(e);
    if (elen < 1e-7) return;
    float invElen = 1.0 / elen;

    // Müller et al. 2007 gradients.
    vec3 grad0 = n1 * elen;
    vec3 grad1 = n2 * elen;
    vec3 grad2 = n1 * (dot(p0 - p3, e) * invElen)
               + n2 * (dot(p1 - p3, e) * invElen);
    vec3 grad3 = n1 * (dot(p2 - p0, e) * invElen)
               + n2 * (dot(p2 - p1, e) * invElen);

    float alphaTilde = (u_dtSubSquared > 0.0) ? (compliance / u_dtSubSquared) : 0.0;
    float wSum = w0 * dot(grad0, grad0)
               + w1 * dot(grad1, grad1)
               + w2 * dot(grad2, grad2)
               + w3 * dot(grad3, grad3);
    if (wSum + alphaTilde < 1e-10) return;

    float lambda = -C / (wSum + alphaTilde);

    // Sign correction: if cross(n1, n2) points along the shared edge, the
    // signed angle is positive — flip lambda to keep the correction in the
    // right direction (matches CPU solver).
    if (dot(cross(n1, n2), e) > 0.0) lambda = -lambda;

    positions[i0].xyz = p0 + grad0 * (w0 * lambda);
    positions[i1].xyz = p1 + grad1 * (w1 * lambda);
    positions[i2].xyz = p2 + grad2 * (w2 * lambda);
    positions[i3].xyz = p3 + grad3 * (w3 * lambda);
}
