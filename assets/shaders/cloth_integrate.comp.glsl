// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_integrate.comp.glsl
/// @brief Phase 9B Step 3 — position prediction from the current velocity.
///
/// One thread per particle. Snapshots the current position into the
/// PreviousPositions SSBO and advances position by `velocity * dt`.
/// Velocity is not written here: damping and the velocity update happen
/// once per substep in `cloth_velocity.comp.glsl`, after the solve
/// (Phase 10.9 Cl10, matching the CPU's step 7). Pinned particles
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

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    float invMass = positions[id].w;
    if (invMass == 0.0) return;  // Pinned: do not move.

    vec3 p = positions[id].xyz;

    prevPositions[id].xyz = p;
    p += velocities[id].xyz * u_deltaTime;

    positions[id].xyz = p;
}
