// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_velocity_stats.comp.glsl
/// @brief Phase 10.9 Cl10 — free-particle speed and kinetic-energy sums.
///
/// Dispatched as ONE workgroup. Each thread strides over the particles,
/// summing for every free particle (positions[i].w > 0, the inverse mass):
/// speed², kinetic energy 0.5·m·|v|² and a count of 1. A shared-memory tree
/// then reduces the 256 partial sums into `stats`:
///   x = sum of speed²   (adaptive damping: avgSpeed = sqrt(x / z))
///   y = sum of KE       (sleep: avgKE = y / z, read back by the CPU)
///   z = free-particle count
/// These are the sums `ClothSimulator::simulate` computes on the CPU for its
/// adaptive damping and sleep detection. Pinned particles are skipped, as
/// there.
///
/// SSBO bindings match `GpuClothSimulator::BufferBinding`.

#version 450 core

layout(local_size_x = 256) in;

layout(std430, binding = 0)  readonly buffer Positions     { vec4 positions[]; };
layout(std430, binding = 2)  readonly buffer Velocities    { vec4 velocities[]; };
layout(std430, binding = 13) writeonly buffer VelocityStats { vec4 stats; };

uniform uint u_particleCount;

shared vec3 s_sum[256];

void main()
{
    uint lid = gl_LocalInvocationID.x;

    vec3 acc = vec3(0.0);
    for (uint i = lid; i < u_particleCount; i += 256u)
    {
        float invMass = positions[i].w;
        if (invMass <= 0.0) continue;  // Pinned.
        float speed2 = dot(velocities[i].xyz, velocities[i].xyz);
        acc += vec3(speed2, 0.5 * speed2 / invMass, 1.0);
    }
    s_sum[lid] = acc;
    barrier();

    for (uint stride = 128u; stride > 0u; stride >>= 1u)
    {
        if (lid < stride)
        {
            s_sum[lid] += s_sum[lid + stride];
        }
        barrier();
    }

    if (lid == 0u)
    {
        stats = vec4(s_sum[0], 0.0);
    }
}
