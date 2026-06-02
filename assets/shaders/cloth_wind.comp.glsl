// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_wind.comp.glsl
/// @brief Phase 9B Step 3 — applies gravity to per-particle velocities.
///        The force "init pass" of the substep, mirroring the CPU substep's
///        step-1 gravity (`cloth_simulator.cpp:293-303`).
///
/// One thread per particle. Reads the velocities SSBO (binding 2) and adds
/// gravity * dt.
///
/// Phase 10.9 Sh4a: the placeholder per-particle "drag toward wind velocity"
/// term that previously lived here was a Step-3-era stand-in — the CPU path
/// has no per-particle drag, only the per-triangle aerodynamic drag in
/// `applyWind`. That real per-triangle drag is now its own colour-grouped
/// pass (`cloth_wind_drag.comp.glsl`), dispatched after this shader and before
/// integration, so this shader is gravity-only. Per-particle FBM wind
/// perturbation (the FULL quality tier) lands in Sh4b.
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
uniform float u_deltaTime;

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_particleCount) return;

    velocities[id].xyz += u_gravity * u_deltaTime;
}
