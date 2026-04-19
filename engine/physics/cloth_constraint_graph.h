// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_constraint_graph.h
/// @brief Pure-CPU helpers for building and graph-colouring cloth constraints.
///
/// Used by `GpuClothSimulator` (Phase 9B Step 4) to lay out distance
/// constraints in an SSBO partitioned by colour: within a colour, no two
/// constraints share a particle, so the GPU can solve the whole colour in
/// parallel without atomics. Greedy graph colouring is run once at
/// `initialize()` time — the resulting partitioning is reused every frame.
///
/// Kept separate from `gpu_cloth_simulator.{h,cpp}` so the colouring and
/// constraint-generation algorithms are testable without a GL context
/// (`tests/test_cloth_constraint_graph.cpp`).
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief GPU-resident layout for a distance constraint (matches std430).
///
/// Two particle indices, the rest length between them, and the XPBD
/// compliance factor (α). Total size is 16 bytes.
struct GpuConstraint
{
    uint32_t i0;
    uint32_t i1;
    float    restLength;
    float    compliance;
};

/// @brief Half-open `[offset, offset+count)` slice into a constraint array.
struct ColourRange
{
    uint32_t offset;
    uint32_t count;
};

/// @brief Appends every stretch and shear distance constraint for a regular
///        cloth grid into @a outConstraints.
///
/// Mirrors the topology built by `ClothSimulator::initialize()` so the GPU
/// backend produces the same drape behaviour as the CPU reference.
///
/// - **Stretch** edges: `(x,z)–(x+1,z)` and `(x,z)–(x,z+1)` — `2·W·H − W − H`
///   constraints for a `W×H` grid.
/// - **Shear** diagonals: `(x,z)–(x+1,z+1)` and `(x,z)–(x−1,z+1)` —
///   `2·(W−1)·(H−1)` constraints.
///
/// Bend (skip-one) constraints are intentionally **not** generated here —
/// they land in Step 5 (`cloth_constraints.comp.glsl` colour expansion).
void generateGridConstraints(
    uint32_t gridW, uint32_t gridH,
    const std::vector<glm::vec3>& positions,
    float stretchCompliance,
    float shearCompliance,
    std::vector<GpuConstraint>& outConstraints);

/// @brief Reorders @a constraints in-place into colour groups.
///
/// Greedy graph colouring: for each constraint, the smallest colour not
/// already used by either endpoint particle is chosen. The resulting
/// constraint vector is sorted by colour (stable within a colour), and the
/// returned ranges describe each colour's slice.
///
/// @param constraints   Constraints to colour, reordered in place.
/// @param particleCount Number of particles in the cloth (sizes the
///                      per-particle colour-tracking buffer).
/// @return Per-colour `[offset, count]` slices into @a constraints.
std::vector<ColourRange> colourConstraints(
    std::vector<GpuConstraint>& constraints,
    uint32_t particleCount);

} // namespace Vestige
