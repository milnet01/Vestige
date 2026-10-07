// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_velocity.comp.glsl
/// @brief Phase 10.9 Cl10 — XPBD velocity recovery with damping.
///
/// One thread per particle, dispatched once at the END of each substep
/// (after the constraint, collision and LRA passes). Velocity is recovered
/// from the net position change of the whole substep —
/// `v = (pos - prevPos) / dt` — then damped by `1 - u_damping`, exactly as
/// the CPU `ClothSimulator::simulate` step 7. A constraint that holds a
/// particle still therefore leaves it at rest, instead of the particle
/// carrying forward the gravity the constraint cancelled. Pinned particles
/// (positions[i].w == 0) get zero velocity, as on the CPU.
///
/// SSBO bindings match `GpuClothSimulator::BufferBinding`.

#version 450 core

layout(local_size_x = 64) in;

layout(std430, binding = 0) buffer Positions      { vec4 positions[]; };
layout(std430, binding = 1) buffer PrevPositions  { vec4 prevPositions[]; };
layout(std430, binding = 2) buffer Velocities     { vec4 velocities[]; };

uniform uint  u_particleCount;
uniform float u_deltaTime;
uniform float u_damping;     // Per-substep velocity scale (0 = no damping).

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    if (positions[id].w == 0.0)
    {
        velocities[id].xyz = vec3(0.0);  // Pinned: at rest.
        return;
    }

    vec3 v = (positions[id].xyz - prevPositions[id].xyz) / u_deltaTime;
    velocities[id].xyz = v * (1.0 - u_damping);
}
