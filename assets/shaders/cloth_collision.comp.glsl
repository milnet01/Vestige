// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_collision.comp.glsl
/// @brief Phase 9B Step 7 — sphere + plane + ground collision response.
///
/// One thread per particle. Pushes the particle out of any sphere or
/// half-space plane it has penetrated by `surface + collisionMargin`, zeros
/// the inward-pointing component of velocity, and applies Coulomb friction
/// as a position correction against the substep's start position. Pinned particles
/// (`positions[i].w == 0`) are skipped. Cylinder, box, and mesh colliders
/// are deferred to a follow-up commit per the design doc.
///
/// SSBO bindings:
///   binding 0 — Positions     (read+write, vec4 xyz + invMass)
///   binding 1 — PrevPositions (read-only, the substep's start positions)
///   binding 2 — Velocities    (read+write, vec4 xyz + padding)
///   binding 3 — Colliders  (UBO, read-only)

#version 450 core

layout(local_size_x = 64) in;

const int MAX_SPHERES = 32;
const int MAX_PLANES  = 16;

struct SphereCollider { vec4 centerRadius; };  // xyz=center, w=radius
struct PlaneCollider  { vec4 normalOffset; };  // xyz=normal (unit), w=offset

layout(std430, binding = 0) buffer Positions  { vec4 positions[]; };
layout(std430, binding = 1) readonly buffer PrevPositions { vec4 prevPositions[]; };
layout(std430, binding = 2) buffer Velocities { vec4 velocities[]; };

layout(std140, binding = 3) uniform Colliders
{
    SphereCollider spheres[MAX_SPHERES];
    PlaneCollider  planes[MAX_PLANES];
    int   sphereCount;
    int   planeCount;
    float groundY;
    float collisionMargin;
    // Coulomb friction coefficients; see applyFriction below.
    float staticFriction;
    float kineticFriction;
    float _pad0;
    float _pad1;
};

uniform uint u_particleCount;

// Coulomb friction at a contact, as a position correction (3D_E-0875).
// Mirrors the applyFriction lambda in `ClothSimulator::applyCollisions`.
// It must act on positions: cloth_velocity.comp.glsl rebuilds velocity from
// (position - prevPosition), so a velocity edit here would be lost.
// `n` is the unit outward contact normal; `depth` is how far the contact just
// pushed the particle out (the normal impulse x dt^2/m). The tangential move
// this substep is cancelled while under staticFriction x depth (stick), else
// shortened by kineticFriction x depth.
void applyFriction(inout vec3 p, vec3 prevP, vec3 n, float depth)
{
    if (depth <= 0.0 || (staticFriction <= 0.0 && kineticFriction <= 0.0)) return;

    vec3  moved      = p - prevP;
    vec3  tangent    = moved - n * dot(moved, n);
    float tangentLen = length(tangent);
    if (tangentLen < 1e-9) return;

    if (tangentLen < staticFriction * depth)
    {
        p -= tangent;  // Static: stick.
    }
    else
    {
        p -= tangent * min(1.0, kineticFriction * depth / tangentLen);
    }
}

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    vec4 posv = positions[id];
    if (posv.w == 0.0) return;  // Pinned: skip.

    vec3 p = posv.xyz;
    vec3 v = velocities[id].xyz;
    vec3 prevP = prevPositions[id].xyz;

    // Ground plane (Y = groundY). Particle stays at or above ground+margin.
    float groundLimit = groundY + collisionMargin;
    if (p.y < groundLimit)
    {
        float depth = groundLimit - p.y;
        p.y = groundLimit;
        if (v.y < 0.0) v.y = 0.0;
        applyFriction(p, prevP, vec3(0.0, 1.0, 0.0), depth);
    }

    // Sphere colliders.
    for (int s = 0; s < sphereCount && s < MAX_SPHERES; ++s)
    {
        vec3  c        = spheres[s].centerRadius.xyz;
        float r        = spheres[s].centerRadius.w + collisionMargin;
        vec3  toP      = p - c;
        float dist     = length(toP);
        if (dist < r && dist > 1e-7)
        {
            vec3 n   = toP / dist;
            p        = c + n * r;
            float vn = dot(v, n);
            if (vn < 0.0) v -= n * vn;
            applyFriction(p, prevP, n, r - dist);
        }
    }

    // Plane colliders: keep particle on the positive-normal side
    // (`dot(p, n) >= offset` is the allowed half-space).
    //
    // Phase 10.9 Sh2 parity fix — no margin for planes. Planes are abstract
    // boundaries (bow-limiters, ceilings), not physical geometry. Adding
    // margin to planes injects energy that accumulates through constraint
    // interaction and causes panels to drift. Matches CPU side at
    // `cloth_simulator.cpp:1163-1166`.
    for (int pl = 0; pl < planeCount && pl < MAX_PLANES; ++pl)
    {
        vec3  n          = planes[pl].normalOffset.xyz;
        float off        = planes[pl].normalOffset.w;
        float signedDist = dot(p, n) - off;
        if (signedDist < 0.0)
        {
            p      -= n * signedDist;  // signedDist < 0, so subtract pushes outward.
            float vn = dot(v, n);
            if (vn < 0.0) v -= n * vn;
            applyFriction(p, prevP, n, -signedDist);
        }
    }

    positions[id].xyz  = p;
    velocities[id].xyz = v;
}
