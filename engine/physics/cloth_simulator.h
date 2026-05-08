// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_simulator.h
/// @brief CPU-based cloth simulation using XPBD (Extended Position-Based Dynamics).
#pragma once

#include "physics/cloth_solver_backend.h"
#include "physics/spatial_hash.h"
#include "utils/deterministic_lcg_rng.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

class ClothMeshCollider;  // Forward declaration

/// @brief Configuration for cloth simulation.
struct ClothConfig
{
    uint32_t width = 20;               ///< Particles along X axis
    uint32_t height = 20;              ///< Particles along Z axis
    float spacing = 0.1f;              ///< Meters between adjacent particles
    float particleMass = 0.1f;         ///< Mass per particle (kg)
    int substeps = 10;                 ///< XPBD substeps per frame
    float stretchCompliance = 0.0f;    ///< 0 = rigid, higher = stretchy
    float shearCompliance = 0.0001f;   ///< Diagonal stretch resistance
    float bendCompliance = 0.01f;      ///< Bending resistance (higher = softer)
    float damping = 0.01f;             ///< Velocity damping per substep (0.01-0.05 typical)
    float sleepThreshold = 0.001f;     ///< Kinetic energy per particle below which cloth sleeps
    glm::vec3 gravity = {0, -9.81f, 0};
};

/// @brief A sphere collider for cloth collision.
struct ClothSphereCollider
{
    glm::vec3 center;
    float radius;
};

/// @brief A half-space plane collider. Particles are pushed to the positive side.
/// The plane is defined as: dot(position, normal) >= offset.
struct ClothPlaneCollider
{
    glm::vec3 normal;  ///< Unit normal pointing toward the allowed half-space
    float offset;      ///< Signed distance from origin along normal
};

/// @brief A vertical cylinder collider (axis-aligned to Y).
/// Particles are pushed outside the cylinder's radius.
struct ClothCylinderCollider
{
    glm::vec3 base;    ///< Center of the cylinder's bottom face
    float radius;      ///< Cylinder radius
    float height;      ///< Cylinder height (extends upward from base.y)
};

/// @brief An axis-aligned box collider. Particles are pushed outside the box.
struct ClothBoxCollider
{
    glm::vec3 min;     ///< Minimum corner (lowest X, Y, Z)
    glm::vec3 max;     ///< Maximum corner (highest X, Y, Z)
};

/// @brief Pure-CPU cloth simulator. No OpenGL or entity dependencies.
///
/// Generates a rectangular grid of particles connected by distance constraints
/// (structural, shear, bending). Uses XPBD for iteration-count-independent
/// stiffness. Supports pin constraints, sphere/plane collision, and wind.
///
/// Implements `IClothSolverBackend` for the per-frame simulation loop so a
/// future `GpuClothSimulator` (Phase 9B GPU compute pipeline) can slot into
/// the same code paths. Configuration mutators below remain on the concrete
/// type during the transitional phase — see
/// `docs/phases/phase_09b_gpu_cloth_design.md` § 4.
class ClothSimulator : public IClothSolverBackend
{
public:
    /// @brief Initializes the cloth grid, particles, and constraints.
    /// @param seed Unique seed for wind randomness (different seed = different timing).
    void initialize(const ClothConfig& config, uint32_t seed = 0) override;

    /// @brief Advances the simulation by deltaTime seconds.
    void simulate(float deltaTime) override;

    /// @brief Phase 10.9 Cl2 — refresh normals from current positions
    /// without integrating physics. Used by `ClothComponent::syncMesh`
    /// after a pin drag or scene-load reset.
    void syncBuffersOnly() override;

    /// @brief Returns the total number of particles.
    uint32_t getParticleCount() const override;

    /// @brief Returns a pointer to the particle positions array.
    const glm::vec3* getPositions() const override;

    /// @brief Returns a pointer to the per-vertex normals (recomputed after simulate).
    const glm::vec3* getNormals() const override;

    /// @brief Returns the grid width (particles along X).
    uint32_t getGridWidth() const override;

    /// @brief Returns the grid height (particles along Z).
    uint32_t getGridHeight() const override;

    /// @brief Returns the triangle index buffer for rendering.
    const std::vector<uint32_t>& getIndices() const override;

    /// @brief Returns the UV coordinates for each particle.
    const std::vector<glm::vec2>& getTexCoords() const override;

    // --- Pin constraints ---

    /// @brief Pins a particle to a fixed world-space position (inverse mass → 0).
    /// @param index Particle index (must be < getParticleCount()).
    /// @param worldPos World-space position to pin the particle at.
    /// @return true if the particle was pinned, false if index is out of bounds.
    bool pinParticle(uint32_t index, const glm::vec3& worldPos) override;

    /// @brief Unpins a particle, restoring its original mass.
    void unpinParticle(uint32_t index) override;

    /// @brief Moves a pinned particle to a new position.
    void setPinPosition(uint32_t index, const glm::vec3& worldPos) override;

    /// @brief Returns true if the given particle is pinned.
    bool isParticlePinned(uint32_t index) const override;

    // --- Collision ---

    /// @brief Adds a sphere collider for the cloth to collide against.
    /// @param center World-space position of the sphere center.
    /// @param radius Radius of the sphere. Must be > 0.
    void addSphereCollider(const glm::vec3& center, float radius) override;

    /// @brief Removes all sphere colliders.
    void clearSphereColliders() override;

    /// @brief Sets the ground plane height (particles are pushed above this Y).
    void setGroundPlane(float height) override;

    /// @brief Returns the current ground plane height.
    float getGroundPlane() const override;

    /// @brief Adds a plane collider (particles stay on the positive-normal side).
    /// @param normal Unit normal pointing toward the allowed half-space (auto-normalized).
    /// @param offset Signed distance from origin along the normal.
    /// @return true if added, false if normal is zero-length.
    bool addPlaneCollider(const glm::vec3& normal, float offset) override;

    /// @brief Adds a vertical cylinder collider (Y-axis aligned).
    /// @param base World-space position of the bottom center of the cylinder (Y-axis aligned).
    /// @param radius Radius of the cylinder cross-section. Must be > 0.
    /// @param height Height of the cylinder extending upward from base.y. Must be > 0.
    void addCylinderCollider(const glm::vec3& base, float radius, float height) override;

    /// @brief Removes all plane colliders.
    void clearPlaneColliders() override;

    /// @brief Removes all cylinder colliders.
    void clearCylinderColliders() override;

    /// @brief Adds an axis-aligned box collider.
    /// @param min Minimum corner of the AABB in world space. Auto-swapped with max if inverted.
    /// @param max Maximum corner of the AABB in world space. Auto-swapped with min if inverted.
    void addBoxCollider(const glm::vec3& min, const glm::vec3& max) override;

    /// @brief Removes all box colliders.
    void clearBoxColliders() override;

    /// @brief Adds a triangle mesh collider (non-owning pointer, caller manages lifetime).
    void addMeshCollider(ClothMeshCollider* collider);

    /// @brief Removes all mesh colliders.
    void clearMeshColliders();

    /// @brief Enables or disables cloth self-collision detection.
    void enableSelfCollision(bool enable);

    /// @brief Sets the minimum distance maintained between non-adjacent particles.
    /// @param distance Cloth thickness (default 0.02m = 2cm).
    void setSelfCollisionDistance(float distance);

    /// @brief Returns true if self-collision is enabled.
    bool isSelfCollisionEnabled() const;

    /// @brief Returns the self-collision distance.
    float getSelfCollisionDistance() const;

    // --- Wind ---

    /// @brief Wind simulation quality tiers.
    /// Full:        Per-particle noise perturbation + per-triangle aerodynamic drag (expensive).
    /// Approximate: Uniform wind (no per-particle noise) + per-triangle drag (moderate).
    /// Simple:      No wind force applied (cheapest).
    ///
    /// **Note:** the canonical name is now `ClothWindQuality` (declared in
    /// `cloth_solver_backend.h` so the polymorphic interface can reference it
    /// without including the full CPU implementation header). `WindQuality`
    /// stays as a backwards-compat alias.
    using WindQuality = ClothWindQuality;

    /// @brief Sets the wind simulation quality tier.
    void setWindQuality(ClothWindQuality quality) override;

    /// @brief Returns the current wind quality tier.
    ClothWindQuality getWindQuality() const override;

    /// @brief Sets the wind direction and strength.
    void setWind(const glm::vec3& direction, float strength) override;

    /// @brief Sets the aerodynamic drag coefficient (default 1.0).
    void setDragCoefficient(float drag) override;

    /// @brief Returns the current wind velocity (direction * strength).
    glm::vec3 getWindVelocity() const override;

    /// @brief Returns the wind direction (unit vector).
    glm::vec3 getWindDirection() const override;

    /// @brief Returns the wind strength scalar.
    float getWindStrength() const override;

    /// @brief Returns the aerodynamic drag coefficient.
    float getDragCoefficient() const override;

    // --- Config ---

    /// @brief Updates the substep count.
    void setSubsteps(int substeps) override;

    /// @brief Returns the current configuration.
    const ClothConfig& getConfig() const override;

    /// @brief Returns true if initialize() has been called.
    bool isInitialized() const override;

    // --- Live parameter updates (no reinit required) ---

    /// @brief Resets simulation to post-initialize state (particles return to initial positions).
    void reset() override;

    /// @brief Captures the current particle positions as the rest/initial state.
    /// Call after repositioning particles (e.g., via pin-all/unpin for XZ→XY conversion).
    void captureRestPositions() override;

    /// @brief Builds Long Range Attachment constraints from current pin/particle positions.
    /// Call after all pins are finalized and captureRestPositions() has been called.
    void rebuildLRA() override;

    /// @brief Updates particle mass for all non-pinned particles.
    void setParticleMass(float mass) override;

    /// @brief Updates damping coefficient.
    void setDamping(float damping) override;

    /// @brief Updates stretch compliance on all stretch constraints.
    void setStretchCompliance(float compliance) override;

    /// @brief Updates shear compliance on all shear constraints.
    void setShearCompliance(float compliance) override;

    /// @brief Updates bend compliance on all bend constraints.
    void setBendCompliance(float compliance) override;

    /// @brief Returns the number of pinned particles.
    uint32_t getPinnedCount() const override;

    /// @brief Returns the total number of constraints (stretch + shear + bend + dihedral).
    uint32_t getConstraintCount() const override;

    /// @brief Returns the number of dihedral bending constraints.
    uint32_t getDihedralConstraintCount() const;

    // --- Dihedral bending ---

    /// @brief Sets the compliance for dihedral bending constraints (0 = rigid, higher = softer).
    void setDihedralBendCompliance(float compliance) override;

    /// @brief Returns the current dihedral bend compliance.
    float getDihedralBendCompliance() const override;

    // --- Adaptive damping ---

    /// @brief Sets the adaptive damping factor (scales damping with average particle speed).
    /// Total damping = baseDamping + adaptiveFactor * avgSpeed. Set to 0 to disable.
    void setAdaptiveDamping(float factor);

    /// @brief Returns the adaptive damping factor.
    float getAdaptiveDamping() const;

    // --- Friction ---

    /// @brief Sets static and kinetic friction coefficients for collider surfaces.
    /// @param staticCoeff Coulomb static friction (default 0.4).
    /// @param kineticCoeff Coulomb kinetic friction (default 0.3).
    void setFriction(float staticCoeff, float kineticCoeff) override;

    /// @brief Returns the static friction coefficient.
    float getStaticFriction() const override;

    /// @brief Returns the kinetic friction coefficient.
    float getKineticFriction() const override;

    // --- Thick particle model ---

    /// @brief Sets the particle radius for thick-particle collision.
    /// Added to all collision margins. Default 0.01m (half of 2cm cloth thickness).
    void setParticleRadius(float radius);

    /// @brief Returns the particle radius.
    float getParticleRadius() const;

    // --- LRA inspection (Phase 10.9 Pe8 — bucketed nearest-pin contract) ---

    /// @brief A Long Range Attachment tether: free particle → its nearest pin.
    /// Public so tests can pin the bucketed-nearest-pin contract directly.
    struct LRAConstraint
    {
        uint32_t particleIndex;   ///< The free particle
        uint32_t pinIndex;        ///< The nearest pinned particle
        float maxDistance;        ///< Distance in rest pose
    };

    /// @brief Returns the current LRA constraint set (post-`rebuildLRA()`).
    const std::vector<LRAConstraint>& getLraConstraints() const { return m_lraConstraints; }

private:
    ClothConfig m_config;
    bool m_initialized = false;

    // SoA particle data
    std::vector<glm::vec3> m_positions;
    std::vector<glm::vec3> m_prevPositions;
    std::vector<glm::vec3> m_velocities;
    std::vector<float> m_inverseMasses;
    std::vector<float> m_originalInverseMasses;  ///< For unpin restore
    std::vector<glm::vec3> m_normals;
    std::vector<glm::vec2> m_texCoords;
    std::vector<glm::vec3> m_initialPositions;   ///< Snapshot for reset()

    // Distance constraints (stretch, shear, bend share the same struct)
    struct DistanceConstraint
    {
        uint32_t i0, i1;
        float restLength;
        float compliance;
    };
    std::vector<DistanceConstraint> m_stretchConstraints;
    std::vector<DistanceConstraint> m_shearConstraints;
    std::vector<DistanceConstraint> m_bendConstraints;

    // Dihedral bending constraints (angle between adjacent triangle normals)
    struct DihedralConstraint
    {
        uint32_t p0, p1, p2, p3;  ///< p0/p1 = wing vertices, p2/p3 = shared edge
        float restAngle;           ///< Rest dihedral angle (radians)
        float compliance;
    };
    std::vector<DihedralConstraint> m_dihedralConstraints;
    float m_dihedralCompliance = 0.01f;  ///< Default soft bending
    void buildDihedralConstraints();
    void solveDihedralConstraint(DihedralConstraint& c, float dtSub);

    // Constraint ordering (BFS depth from pins)
    std::vector<uint32_t> m_particleDepth;    ///< BFS depth from nearest pin
    bool m_constraintsSorted = false;
    void sortConstraintsByDepth();

    // Pin constraints
    struct PinConstraint
    {
        uint32_t index;
        glm::vec3 position;
    };
    std::vector<PinConstraint> m_pinConstraints;

    // Long Range Attachment (LRA) constraints — prevent cumulative drift from pins.
    // Each free particle is tethered to its nearest pinned particle with a maximum
    // distance equal to the rest-pose distance. Unilateral: only activates when the
    // particle drifts too far, so it doesn't fight wind or natural draping.
    // Used by NvCloth, PhysX, Jolt, Obi Cloth. (Kim, Chentanez, Müller, SCA 2012)
    // Struct definition lives in the public section so tests can inspect.
    std::vector<LRAConstraint> m_lraConstraints;
    void buildLRAConstraints();    ///< Called after pins are finalized
    void solveLRAConstraints();    ///< Called after distance constraints each substep

    // Collision primitives
    std::vector<ClothSphereCollider> m_sphereColliders;
    std::vector<ClothPlaneCollider> m_planeColliders;
    std::vector<ClothCylinderCollider> m_cylinderColliders;
    std::vector<ClothBoxCollider> m_boxColliders;
    std::vector<ClothMeshCollider*> m_meshColliders;  ///< Non-owning pointers
    float m_groundPlaneY = -1000.0f;

    // Self-collision
    bool m_selfCollision = false;
    float m_selfCollisionDist = 0.02f;  ///< 2cm default cloth thickness
    SpatialHash m_spatialHash;
    std::vector<uint32_t> m_selfCollisionNeighbors;  ///< Reused per query
    void applySelfCollision();
    bool areGridAdjacent(uint32_t i, uint32_t j) const;

    // Adaptive damping
    float m_adaptiveDampingFactor = 0.0f;  ///< 0 = disabled, typical 0.1-0.5

    // Friction (Coulomb model)
    float m_staticFriction = 0.4f;    ///< μs
    float m_kineticFriction = 0.3f;   ///< μk

    // Thick particle model
    float m_particleRadius = 0.0f;    ///< 0 = point particles (legacy behavior)

    // Wind
    glm::vec3 m_windDirection = glm::vec3(0.0f);
    float m_windStrength = 0.0f;
    float m_dragCoeff = 1.0f;
    WindQuality m_windQuality = WindQuality::FULL;
    float m_elapsed = 0.0f;

    // Gust state machine: creates realistic blow/calm cycles
    float m_gustCurrent = 0.0f;       ///< Current gust intensity [0,1]
    float m_gustTarget = 0.0f;        ///< Target gust intensity
    float m_gustTimer = 0.0f;         ///< Time until next target change
    float m_gustRampSpeed = 0.0f;     ///< How fast to reach target
    glm::vec3 m_windDirOffset = glm::vec3(0.0f);  ///< Current direction offset
    glm::vec3 m_windDirTarget = glm::vec3(0.0f);  ///< Target direction offset
    float m_dirTimer = 0.0f;          ///< Time until next direction change
    DeterministicLcgRng m_rng{12345u};  ///< Shared LCG for deterministic randomness

    void updateGustState(float dt);

    // Grid topology
    uint32_t m_gridW = 0;
    uint32_t m_gridH = 0;
    std::vector<uint32_t> m_indices;

    // Sleep state: particles freeze when kinetic energy drops below threshold
    bool m_sleeping = false;
    int m_sleepFrames = 0;           ///< Consecutive frames below threshold
    static constexpr int SLEEP_FRAME_COUNT = 3;  ///< Frames below threshold before sleeping

    // Solver internals
    void solveDistanceConstraint(DistanceConstraint& c, float alphaTilde, float dtSub);
    void solvePinConstraints();
    void applyCollisions();
    /// @brief Phase 10.9 Pe9 — rebuilds the self-collision spatial hash
    ///        once per substep (no-op if self-collision is disabled or
    ///        the cloth has fewer than 4 particles). Hoisted out of
    ///        `applySelfCollision()` so both `applyCollisions()` passes
    ///        in a single substep share the same broad-phase data.
    void rebuildSelfCollisionHashIfEnabled();
    void recomputeNormals();
    void precomputeWind();     ///< Cache noise-dependent wind data once per simulate()
    void applyWind(float dt);  ///< Apply cached wind data per substep

    // Cached per-frame wind data (computed once in precomputeWind, used N times in applyWind)
    std::vector<glm::vec3> m_cachedParticleWind;  ///< Per-particle perturbation strength
    std::vector<float> m_cachedTriangleTurb;       ///< Per-triangle spatial turbulence factor
    float m_cachedFlutter = 1.0f;                  ///< Cached flutter value for this frame
    bool m_windPrecomputed = false;                ///< True after precomputeWind() runs
};

} // namespace Vestige
