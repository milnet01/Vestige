// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_wind_fbm.comp.glsl
/// @brief Phase 10.9 Sh4b — per-particle FBM wind perturbation (FULL tier only).
///
/// One thread per particle. Adds the cached per-particle wind perturbation to
/// the velocity, mirroring the CPU `ClothSimulator::applyWind` FULL-tier block
/// (the `m_velocities[i] += m_cachedParticleWind[i] * dt` loop).
///
/// The perturbation in `ParticleWindFbm` is computed once per frame on the CPU
/// by `ClothWindModel::precompute` and uploaded; it is already pre-multiplied
/// by the per-particle strength factor AND the inverse mass, and pinned
/// particles (invMass == 0) are stored as zero. So this pass needs no pin
/// branch and no invMass multiply — it just scales by the substep dt, exactly
/// like the CPU loop.
///
/// Dispatched once per substep BEFORE the per-triangle drag pass
/// (`cloth_wind_drag.comp.glsl`) so the drag's vAvg reads the already-perturbed
/// velocities — matching the CPU step ordering.
///
/// SSBO bindings match `GpuClothSimulator::BufferBinding`:
///   binding  2 — Velocities      (read+write; vec4, w unused)
///   binding 10 — ParticleWindFbm (read-only;  vec4, xyz = perturbation, w pad)

#version 450 core

layout(local_size_x = 64) in;

layout(std430, binding = 2) buffer Velocities
{
    vec4 velocities[];
};

layout(std430, binding = 10) readonly buffer ParticleWindFbm
{
    vec4 particleWind[];  // xyz = pre-scaled perturbation (incl. invMass), w pad
};

uniform uint  u_particleCount;
uniform float u_deltaTime;

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    velocities[id].xyz += particleWind[id].xyz * u_deltaTime;
}
