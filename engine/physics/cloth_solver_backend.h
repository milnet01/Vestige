// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_solver_backend.h
/// @brief Abstract backend interface for cloth simulators (CPU XPBD or GPU compute).
///
/// Phase 9B GPU compute cloth pipeline. Both `ClothSimulator` (CPU XPBD) and
/// `GpuClothSimulator` (GPU compute) implement this contract so callers
/// (`ClothComponent`, the inspector panel, engine bookkeeping) can drive
/// either backend through a single pointer.
///
/// Phase 9B Step 12 widened the interface from "lifecycle only" to cover the
/// full mutator + accessor surface used by call sites — pin / collider /
/// wind / live-tuning setters, plus the auxiliary getters the inspector panel
/// reads each frame for diagnostics.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

struct ClothConfig;  // Forward declaration — full definition in cloth_simulator.h.

/// @brief Wind simulation quality tier.
///
/// Pulled out of `ClothSimulator` (where it used to be a nested enum) so the
/// `IClothSolverBackend` mutator surface can reference it without including
/// the full CPU implementation header.
enum class ClothWindQuality
{
    FULL        = 0,   ///< Per-particle noise + per-triangle aerodynamic drag.
    APPROXIMATE = 1,   ///< Uniform wind (no per-particle noise) + per-triangle drag.
    SIMPLE      = 2,   ///< No wind force applied.
};

/// @brief Hard upper bound on substeps per frame for every cloth backend.
///
/// CPU `ClothSimulator::simulate` historically clamped to `[1, 64]` silently
/// inside the per-frame loop while `setSubsteps` only clamped the lower bound;
/// the GPU backend had no upper cap at all. Phase 10.9 Cl7 unifies both
/// backends on this single constant so a stray `setSubsteps(10000)` call from
/// the inspector / a preset can't burn a whole frame stepping the cloth.
inline constexpr int MAX_SUBSTEPS = 64;

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

    /// @brief Refresh the renderer-facing buffers (normals, GPU mirror)
    /// from the *current* particle positions without integrating gravity,
    /// wind, or collisions. Phase 10.9 Cl2 — `ClothComponent::syncMesh`
    /// previously called `simulate(0.0001f)` to force a normal-recompute
    /// after a pin drag or scene-load reset, which silently injected a
    /// 100 µs gravity tick into a refresh that should have been pure
    /// readback. CPU backend recomputes normals in-place; GPU backend
    /// re-runs only the normals shader + flags the mirror dirty.
    virtual void syncBuffersOnly() = 0;

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

    /// @brief Live configuration snapshot (read-only). Backends keep their
    /// own internal config copy; this returns a stable reference into it.
    virtual const ClothConfig& getConfig() const = 0;

    // -- Live tuning (no reinit required) --

    virtual void setSubsteps(int substeps) = 0;
    virtual void setParticleMass(float mass) = 0;
    virtual void setDamping(float damping) = 0;
    virtual void setStretchCompliance(float compliance) = 0;
    virtual void setShearCompliance(float compliance) = 0;
    virtual void setBendCompliance(float compliance) = 0;

    /// @brief Phase 10.9 Cl4 — XPBD compliance for the dihedral bending
    ///        solver (separate from the distance-bend constraint exposed
    ///        by `setBendCompliance`). 0 = perfectly stiff, larger values
    ///        let creases form more readily. Both backends now expose
    ///        the live tuning surface (CPU updates each constraint in
    ///        place; GPU re-uploads the dihedral SSBO).
    virtual void setDihedralBendCompliance(float compliance) = 0;
    virtual float getDihedralBendCompliance() const = 0;

    // -- Wind --

    virtual void setWind(const glm::vec3& direction, float strength) = 0;
    virtual void setDragCoefficient(float drag) = 0;
    virtual void setWindQuality(ClothWindQuality quality) = 0;
    virtual glm::vec3       getWindVelocity() const = 0;
    virtual glm::vec3       getWindDirection() const = 0;
    virtual float           getWindStrength() const = 0;
    virtual float           getDragCoefficient() const = 0;
    virtual ClothWindQuality getWindQuality() const = 0;

    // -- Pins / LRA --

    /// @brief Pin a particle to a fixed world-space position. Returns false if
    /// `index` is out of bounds.
    virtual bool pinParticle(uint32_t index, const glm::vec3& worldPos) = 0;
    virtual void unpinParticle(uint32_t index) = 0;
    virtual void setPinPosition(uint32_t index, const glm::vec3& worldPos) = 0;
    virtual bool isParticlePinned(uint32_t index) const = 0;
    virtual uint32_t getPinnedCount() const = 0;

    /// @brief Snapshot current positions as the rest pose (used by `reset()` and
    /// after pin reconfiguration).
    virtual void captureRestPositions() = 0;

    /// @brief Rebuild Long-Range Attachment tethers from the current pin set.
    virtual void rebuildLRA() = 0;

    // -- Constraint diagnostics (inspector panel) --

    /// @brief Total distance constraint count (stretch + shear + bend).
    virtual uint32_t getConstraintCount() const = 0;

    // -- Collider mutators --
    //
    // Sphere / plane / ground are supported on both backends. Cylinder / box /
    // mesh are CPU-only today (GPU support is deferred per the design doc);
    // GPU backends implement them as a one-line log-and-drop so callers can
    // still drive a single code path across both backends.

    virtual void addSphereCollider(const glm::vec3& center, float radius) = 0;
    virtual void clearSphereColliders() = 0;

    virtual bool addPlaneCollider(const glm::vec3& normal, float offset) = 0;
    virtual void clearPlaneColliders() = 0;

    virtual void setGroundPlane(float worldY) = 0;
    virtual float getGroundPlane() const = 0;

    virtual void addCylinderCollider(const glm::vec3& base, float radius, float height) = 0;
    virtual void clearCylinderColliders() = 0;

    virtual void addBoxCollider(const glm::vec3& min, const glm::vec3& max) = 0;
    virtual void clearBoxColliders() = 0;

    // -- Friction --
    //
    // Phase 10.9 Sh3 — Coulomb static + kinetic friction at every collider
    // contact (ground / sphere / plane on both backends; cylinder / box / mesh
    // on CPU only since those colliders are CPU-only). Negative inputs clamp
    // to zero. CPU defaults are 0.4 / 0.3; GPU defaults match.

    /// @brief Sets static and kinetic friction coefficients for collider surfaces.
    virtual void setFriction(float staticCoeff, float kineticCoeff) = 0;

    /// @brief Returns the static friction coefficient.
    virtual float getStaticFriction() const = 0;

    /// @brief Returns the kinetic friction coefficient.
    virtual float getKineticFriction() const = 0;
};

} // namespace Vestige
