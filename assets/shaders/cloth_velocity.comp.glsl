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
/// Two CPU settle-down steps ride along, in the CPU's order:
/// - Rest-pose blend (CPU step 6): before the recovery, a free particle moves
///   `u_restBlend` of the way to its rest position. The caller passes
///   0.015 × (1 − gust) for cloth with LRA tethers, else 0.
/// - Adaptive damping: with `u_adaptiveFactor > 0` the damping becomes
///   min(u_damping + factor × avgSpeed, 0.95), avgSpeed = sqrt(Σspeed² / n)
///   from the frame-start sums cloth_velocity_stats wrote.
///
/// SSBO bindings match `GpuClothSimulator::BufferBinding`.

#version 450 core

layout(local_size_x = 64) in;

layout(std430, binding = 0)  buffer Positions              { vec4 positions[]; };
layout(std430, binding = 1)  buffer PrevPositions          { vec4 prevPositions[]; };
layout(std430, binding = 2)  buffer Velocities             { vec4 velocities[]; };
layout(std430, binding = 12) readonly buffer RestPositions { vec4 restPositions[]; };
layout(std430, binding = 13) readonly buffer VelocityStats { vec4 stats; };  // x Σspeed², z count

uniform uint  u_particleCount;
uniform float u_deltaTime;
uniform float u_damping;         // Per-substep velocity scale (0 = no damping).
uniform float u_restBlend;       // 0 = no rest-pose blend this substep.
uniform float u_adaptiveFactor;  // 0 = adaptive damping off.

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    if (positions[id].w == 0.0)
    {
        velocities[id].xyz = vec3(0.0);  // Pinned: at rest.
        return;
    }

    vec3 pos = positions[id].xyz;
    if (u_restBlend > 0.0)
    {
        pos = mix(pos, restPositions[id].xyz, u_restBlend);
        positions[id].xyz = pos;
    }

    float damping = u_damping;
    if (u_adaptiveFactor > 0.0 && stats.z > 0.0)
    {
        damping = min(u_damping + u_adaptiveFactor * sqrt(stats.x / stats.z), 0.95);
    }

    vec3 v = (pos - prevPositions[id].xyz) / u_deltaTime;
    velocities[id].xyz = v * (1.0 - damping);
}
