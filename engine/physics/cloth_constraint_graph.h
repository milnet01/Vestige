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

/// @brief GPU-resident layout for a dihedral bending constraint (std430).
///
/// Four particle indices (`p0`/`p1` are the wing vertices opposite the shared
/// edge; `p2`/`p3` are the shared-edge endpoints), the rest dihedral angle
/// between the two adjacent triangles, and the XPBD compliance factor.
/// Padded to 32 bytes to satisfy std430 alignment when packed as a uvec4 +
/// vec4 in the GLSL `cloth_dihedral.comp.glsl` shader.
struct GpuDihedralConstraint
{
    uint32_t p0;
    uint32_t p1;
    uint32_t p2;
    uint32_t p3;
    float    restAngle;
    float    compliance;
    float    pad0;
    float    pad1;
};

/// @brief Half-open `[offset, offset+count)` slice into a constraint array.
struct ColourRange
{
    uint32_t offset;
    uint32_t count;
};

/// @brief Appends every stretch, shear, and bend distance constraint for a
///        regular cloth grid into @a outConstraints.
///
/// Mirrors the topology built by `ClothSimulator::initialize()` so the GPU
/// backend produces the same drape behaviour as the CPU reference.
///
/// - **Stretch** edges: `(x,z)–(x+1,z)` and `(x,z)–(x,z+1)` — `2·W·H − W − H`
///   constraints for a `W×H` grid.
/// - **Shear** diagonals: `(x,z)–(x+1,z+1)` and `(x,z)–(x−1,z+1)` —
///   `2·(W−1)·(H−1)` constraints.
/// - **Bend** (skip-one) edges: `(x,z)–(x+2,z)` and `(x,z)–(x,z+2)` —
///   `(W−2)·H + W·(H−2)` constraints. They share the same XPBD distance-
///   constraint solver as stretch/shear; only the rest length and compliance
///   differ.
void generateGridConstraints(
    uint32_t gridW, uint32_t gridH,
    const std::vector<glm::vec3>& positions,
    float stretchCompliance,
    float shearCompliance,
    float bendCompliance,
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

/// @brief Builds dihedral bending constraints from a triangle index buffer.
///
/// For every edge shared by exactly two triangles ("interior edge"), emits a
/// dihedral constraint between the two wing vertices and the two edge
/// endpoints, with rest angle equal to the current angle between the
/// triangles' face normals. Boundary edges (shared by only one triangle) and
/// non-manifold edges (shared by three or more) are skipped.
void generateDihedralConstraints(
    const std::vector<uint32_t>& indices,
    const std::vector<glm::vec3>& positions,
    float compliance,
    std::vector<GpuDihedralConstraint>& outConstraints);

/// @brief Greedy graph-colours dihedral constraints (4 particles each).
///
/// Same algorithm as `colourConstraints` for the 2-particle case, generalised
/// to four particles. Within a colour no two dihedral constraints touch any
/// of the same four particles.
std::vector<ColourRange> colourDihedralConstraints(
    std::vector<GpuDihedralConstraint>& constraints,
    uint32_t particleCount);

/// @brief GPU-resident layout for a Long-Range Attachment constraint.
///
/// Each free particle is tethered to its nearest pinned particle. Unilateral:
/// the constraint only activates when the particle has drifted past
/// `maxDistance` from its pin. No graph colouring is needed because each LRA
/// writes only its own particle (the pin is read-only).
struct GpuLraConstraint
{
    uint32_t particleIndex;
    uint32_t pinIndex;
    float    maxDistance;
    float    pad;
};

/// @brief Builds LRA constraints from a particle set + pin set.
///
/// For each unpinned particle, finds the nearest pinned particle and emits an
/// LRA tether equal to their current Euclidean distance. Returns no-op if the
/// pin set is empty.
void generateLraConstraints(
    const std::vector<glm::vec3>& positions,
    const std::vector<uint32_t>& pinIndices,
    std::vector<GpuLraConstraint>& outConstraints);

} // namespace Vestige
