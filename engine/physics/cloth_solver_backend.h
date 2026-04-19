// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_solver_backend.h
/// @brief Abstract backend interface for cloth simulators (CPU XPBD or GPU compute).
///
/// Phase 9B GPU compute cloth pipeline introduces a second backend
/// (`GpuClothSimulator`) alongside the existing CPU `ClothSimulator`. Both
/// implement this interface so the per-frame simulation loop is interchangeable.
///
/// **Scope:** Only the per-frame simulation loop and the data-readback surface
/// (positions / normals / indices / UVs) are virtual. Configuration mutators
/// (`setWind`, `addSphereCollider`, `pinParticle`, etc.) remain on the concrete
/// `ClothSimulator` type during this transitional phase. Once the GPU backend
/// implements its own configuration API, the interface will widen — see
/// `docs/PHASE9B_GPU_CLOTH_DESIGN.md` § 4 (`IClothSolverBackend interface`).
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

struct ClothConfig;  // Forward declaration — full definition in cloth_simulator.h.

/// @brief Per-frame simulation contract shared by every cloth-solver backend.
///
/// Implementations must:
/// 1. Allocate particle / constraint storage in `initialize()`.
/// 2. Advance the simulation by a wall-clock delta in `simulate()`.
/// 3. Expose particle positions / normals / mesh topology to the renderer.
/// 4. Be safely destroyable via `delete` through the base pointer.
class IClothSolverBackend
{
public:
    virtual ~IClothSolverBackend() = default;

    // -- Lifecycle --

    /// @brief Allocates buffers and seeds the cloth grid.
    /// @param config Grid dimensions, masses, compliances, gravity.
    /// @param seed   Per-cloth wind randomness seed (0 = default).
    virtual void initialize(const ClothConfig& config, uint32_t seed = 0) = 0;

    /// @brief Advances the simulation by `deltaTime` seconds.
    virtual void simulate(float deltaTime) = 0;

    /// @brief Returns the cloth to its post-`initialize()` rest state.
    virtual void reset() = 0;

    /// @brief True once `initialize()` has succeeded.
    virtual bool isInitialized() const = 0;

    // -- Per-frame readback (renderer-facing) --

    /// @brief Number of particles in the grid.
    virtual uint32_t getParticleCount() const = 0;

    /// @brief Pointer to a `getParticleCount()`-sized array of world-space positions.
    /// May trigger a GPU→CPU readback for GPU backends.
    virtual const glm::vec3* getPositions() const = 0;

    /// @brief Pointer to a `getParticleCount()`-sized array of per-vertex normals.
    /// May trigger a GPU→CPU readback for GPU backends.
    virtual const glm::vec3* getNormals() const = 0;

    /// @brief Triangle index buffer (3 indices per triangle).
    virtual const std::vector<uint32_t>& getIndices() const = 0;

    /// @brief Per-vertex UV coordinates.
    virtual const std::vector<glm::vec2>& getTexCoords() const = 0;

    // -- Grid topology --

    /// @brief Particles along the X axis.
    virtual uint32_t getGridWidth() const = 0;

    /// @brief Particles along the Z axis.
    virtual uint32_t getGridHeight() const = 0;
};

} // namespace Vestige
