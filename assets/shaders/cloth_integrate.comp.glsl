// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_integrate.comp.glsl
/// @brief Phase 9B Step 3 — symplectic Euler integration with velocity damping.
///
/// One thread per particle. Snapshots the current position into the
/// PreviousPositions SSBO, dampens the velocity, advances position by
/// `velocity * dt`, then writes both back. Pinned particles
/// (positions[i].w == 0) are skipped — that w channel doubles as inverse
/// mass per the design doc § 4 (`vec4` layout note). Step 9 will populate
/// the inverse-mass channel from `LRA` / pin state; until then every
/// particle's w is 1 (free).
///
/// SSBO bindings match `GpuClothSimulator::BufferBinding`.

#version 450 core

layout(local_size_x = 64) in;

layout(std430, binding = 0) buffer Positions      { vec4 positions[]; };
layout(std430, binding = 1) buffer PrevPositions  { vec4 prevPositions[]; };
layout(std430, binding = 2) buffer Velocities     { vec4 velocities[]; };

uniform uint  u_particleCount;
uniform float u_deltaTime;
uniform float u_damping;     // Per-step velocity scale (0 = no damping).

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    float invMass = positions[id].w;
    if (invMass == 0.0) return;  // Pinned: do not move.

    vec3 p = positions[id].xyz;
    vec3 v = velocities[id].xyz * (1.0 - u_damping);

    prevPositions[id].xyz = p;
    p += v * u_deltaTime;

    positions[id].xyz  = p;
    velocities[id].xyz = v;
}
