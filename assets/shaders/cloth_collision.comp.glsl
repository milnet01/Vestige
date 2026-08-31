// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_collision.comp.glsl
/// @brief Phase 9B Step 7 — sphere + plane + ground collision response.
///
/// One thread per particle. Pushes the particle out of any sphere or
/// half-space plane it has penetrated by `surface + collisionMargin`, and
/// zeros the inward-pointing component of velocity. Pinned particles
/// (`positions[i].w == 0`) are skipped. Cylinder, box, and mesh colliders
/// are deferred to a follow-up commit per the design doc.
///
/// SSBO bindings:
///   binding 0 — Positions  (read+write, vec4 xyz + invMass)
///   binding 2 — Velocities (read+write, vec4 xyz + padding)
///   binding 3 — Colliders  (UBO, read-only)

#version 450 core

layout(local_size_x = 64) in;

const int MAX_SPHERES = 32;
const int MAX_PLANES  = 16;

struct SphereCollider { vec4 centerRadius; };  // xyz=center, w=radius
struct PlaneCollider  { vec4 normalOffset; };  // xyz=normal (unit), w=offset

layout(std430, binding = 0) buffer Positions  { vec4 positions[]; };
layout(std430, binding = 2) buffer Velocities { vec4 velocities[]; };

layout(std140, binding = 3) uniform Colliders
{
    SphereCollider spheres[MAX_SPHERES];
    PlaneCollider  planes[MAX_PLANES];
    int   sphereCount;
    int   planeCount;
    float groundY;
    float collisionMargin;
    // Phase 10.9 Sh3 — Coulomb friction. Matches CPU
    // `ClothSimulator::applyFriction` two-tier model: static if
    // `|vTangent| < staticFriction · |vNormal|`, else kinetic reduction.
    float staticFriction;
    float kineticFriction;
    float _pad0;
    float _pad1;
};

uniform uint u_particleCount;

// Phase 10.9 Sh3 — Coulomb friction at a contact. Mirrors the CPU helper at
// `cloth_simulator.cpp:990-1016`. `n` must be the unit outward contact
// normal. Modifies `v` in place.
//
// Invariant: called *after* the inward velocity component has been zeroed
// out by the caller (the same pattern the CPU side uses). That is exactly why
// `normalImpulse` is a PARAMETER: the residual dot(v, n) at that point is 0 by
// construction, so deriving the normal speed from `v` here made the whole
// function a no-op on every real contact. The caller passes the inward speed
// it removed, which is the impulse Coulomb friction is proportional to.
void applyFriction(inout vec3 v, vec3 n, float normalImpulse)
{
    if (staticFriction <= 0.0 && kineticFriction <= 0.0) return;

    float vn       = dot(v, n);
    vec3  vNormal  = n * vn;
    vec3  vTangent = v - vNormal;
    float vtLen    = length(vTangent);
    if (vtLen < 1e-7) return;

    float normalSpeed = normalImpulse;
    if (vtLen < staticFriction * normalSpeed)
    {
        v = vNormal;  // Static: stick.
    }
    else
    {
        float reduction = kineticFriction * normalSpeed / vtLen;
        v = vNormal + vTangent * max(0.0, 1.0 - reduction);
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

    // Ground plane (Y = groundY). Particle stays at or above ground+margin.
    float groundLimit = groundY + collisionMargin;
    if (p.y < groundLimit)
    {
        p.y = groundLimit;
        // Capture the inward normal speed BEFORE it is zeroed -- applyFriction
        // needs the impulse that was just removed. Reading dot(v, n) after the
        // zeroing gave normalSpeed == 0 exactly, which made the static test
        // `vtLen < 0` never true and the kinetic reduction `k * 0 / vtLen` a
        // no-op: friction was arithmetically inert on every contact.
        float impulseY = max(0.0, -v.y);
        if (v.y < 0.0) v.y = 0.0;
        applyFriction(v, vec3(0.0, 1.0, 0.0), impulseY);
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
            float impulse = max(0.0, -vn);  // see the ground case above
            if (vn < 0.0) v -= n * vn;
            applyFriction(v, n, impulse);
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
            float impulse = max(0.0, -vn);  // see the ground case above
            if (vn < 0.0) v -= n * vn;
            applyFriction(v, n, impulse);
        }
    }

    positions[id].xyz  = p;
    velocities[id].xyz = v;
}
