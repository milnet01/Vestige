// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_lra.comp.glsl
/// @brief Phase 9B Step 9 — Long-Range Attachment unilateral tether solver.
///
/// One thread per LRA constraint. Each constraint tethers a free particle to
/// its nearest pinned particle with a maximum distance equal to the rest-pose
/// distance. Unilateral: only activates when the particle has drifted past
/// `maxDistance` (so it doesn't fight wind or natural draping at rest).
///
/// No graph colouring is needed: each thread is the unique writer of its
/// own particle's position, and the pin's position is read-only.
///
/// SSBO bindings:
///   binding 0 — Positions (read+write, vec4 xyz + invMass)
///   binding 8 — LraConstraints (read-only)

#version 450 core

layout(local_size_x = 64) in;

struct LraConstraint
{
    uint  particle;
    uint  pin;
    float maxDistance;
    float pad;
};

layout(std430, binding = 0) buffer Positions
{
    vec4 positions[];
};

layout(std430, binding = 8) readonly buffer Lras
{
    LraConstraint lras[];
};

uniform uint u_lraCount;

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_lraCount) return;

    LraConstraint c = lras[id];

    vec3 pinPos = positions[c.pin].xyz;
    vec3 p      = positions[c.particle].xyz;

    vec3  delta = p - pinPos;
    float dist  = length(delta);

    // Unilateral: only pull back if the particle has drifted past the tether.
    if (dist > c.maxDistance && dist > 1e-7)
    {
        positions[c.particle].xyz = pinPos + delta * (c.maxDistance / dist);
    }
}
