// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_constraints.comp.glsl
/// @brief Phase 9B Step 4 — XPBD distance-constraint solver, parameterised by colour.
///
/// One thread per constraint within the dispatched colour range. Loads both
/// endpoint particles, computes the XPBD position correction
/// `Δp = -C / (w0 + w1 + α̃) · n` where `α̃ = compliance / dt²`, and writes
/// both endpoints back to the positions SSBO. Within a colour no two
/// constraints share a particle (CPU-side greedy graph colouring guarantees
/// this), so the writes are race-free without atomics.
///
/// **Small-steps XPBD.** This shader implements the Macklin "Small Steps in
/// Physics Simulation" 2018 variant: the Lagrange multiplier λ is recomputed
/// from `C` each substep with no across-iteration accumulator. The canonical
/// XPBD formulation (Macklin et al. 2016 §3.5) accumulates Σλ across the
/// inner Gauss-Seidel iterations within a single substep; the small-steps
/// variant trades that accumulator for a higher substep count and is
/// equivalent under the convergence regime cloth runs in (≥ 10 substeps —
/// see `IClothSolverBackend::setSubsteps`, default 10). The CPU path makes
/// the same trade explicitly at `cloth_simulator.cpp:961` ("λ = 0
/// per-substep (reset each substep in XPBD small-steps approach)") so
/// CPU/GPU stay parameter-equivalent. Phase 10.9 Sh1 — header tightened
/// after a /indie-review reviewer flagged the shader as "PBD-with-compliance,
/// not XPBD"; the underlying math is small-steps XPBD, not classical PBD.
///
/// Pinned particles are encoded with `positions[i].w == 0` (inverse mass).
/// `wSum == 0` (both pinned) short-circuits to avoid div-by-zero when
/// compliance is also zero.
///
/// SSBO bindings:
///   binding 0 — Positions (read+write, vec4 xyz + invMass)
///   binding 4 — Constraints (read-only, GpuConstraint matching the C++ struct)

#version 450 core

layout(local_size_x = 64) in;

struct Constraint
{
    uint  i0;
    uint  i1;
    float restLength;
    float compliance;
};

layout(std430, binding = 0) buffer Positions
{
    vec4 positions[];
};

layout(std430, binding = 4) readonly buffer Constraints
{
    Constraint constraints[];
};

uniform uint  u_colorOffset;     // First constraint index for this colour group.
uniform uint  u_colorCount;      // Constraints in this colour group.
uniform float u_dtSubSquared;    // dt² of the current substep (for α̃ = α / dt²).

void main()
{
    uint id = gl_GlobalInvocationID.x;
    if (id >= u_colorCount) return;

    Constraint c = constraints[u_colorOffset + id];

    vec4 p0v = positions[c.i0];
    vec4 p1v = positions[c.i1];

    float w0 = p0v.w;
    float w1 = p1v.w;
    float wSum = w0 + w1;
    if (wSum == 0.0) return;     // Both pinned: skip.

    vec3 delta = p0v.xyz - p1v.xyz;
    float dist = length(delta);
    if (dist < 1e-7) return;

    vec3 n = delta / dist;
    float C = dist - c.restLength;

    float alphaTilde = (u_dtSubSquared > 0.0) ? (c.compliance / u_dtSubSquared) : 0.0;
    float lambda    = -C / (wSum + alphaTilde);

    vec3 dp = lambda * n;

    positions[c.i0].xyz = p0v.xyz + w0 * dp;
    positions[c.i1].xyz = p1v.xyz - w1 * dp;
}
