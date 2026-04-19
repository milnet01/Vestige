// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_wind.comp.glsl
/// @brief Phase 9B Step 3 — applies external forces (gravity + uniform wind)
///        to per-particle velocities.
///
/// One thread per particle. Reads the velocities SSBO (binding 2),
/// adds gravity * dt, then adds an aerodynamic-drag term that pushes
/// velocity toward the wind velocity. Per-particle noise and per-triangle
/// drag (the FULL quality tier on the CPU path) land in a later step
/// alongside the rest of the wind-quality tiers.
///
/// SSBO bindings match `GpuClothSimulator::BufferBinding` in
/// `engine/physics/gpu_cloth_simulator.h` — keep them in sync if you
/// reorder.

#version 450 core

layout(local_size_x = 64) in;

layout(std430, binding = 2) buffer Velocities
{
    vec4 velocities[];
};

uniform uint  u_particleCount;
uniform vec3  u_gravity;
uniform vec3  u_windVelocity;
uniform float u_dragCoeff;
uniform float u_deltaTime;

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    vec3 v = velocities[id].xyz;

    // Gravity.
    v += u_gravity * u_deltaTime;

    // Aerodynamic drag toward the wind velocity. Coefficient is the same
    // dragCoefficient the CPU path's APPROXIMATE wind tier uses, so visual
    // parity is achievable at fit-time.
    vec3 relWind = u_windVelocity - v;
    v += relWind * u_dragCoeff * u_deltaTime;

    velocities[id].xyz = v;
}
