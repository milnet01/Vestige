// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_cloth_simulator.h
/// @brief GPU compute-shader backend for cloth simulation (Phase 9B skeleton).
///
/// Implements `IClothSolverBackend` using SSBOs for particle state. The
/// per-substep work (force accumulation, integration, constraint solving,
/// collision, normal recomputation) lands incrementally over Steps 3–9 of
/// the Phase 9B GPU cloth pipeline — see `docs/phases/phase_09b_gpu_cloth_design.md`.
///
/// **Step 2 scope:** SSBO allocation + teardown only. `simulate()` is a no-op.
/// `getPositions()` / `getNormals()` return a CPU mirror that holds the
/// initial grid state (the skeleton does not move particles). Future steps
/// will dispatch compute shaders and switch the mirror to a lazy GPU→CPU
/// readback per the design doc.
///
/// **Context requirement:** `initialize()` and the destructor MUST run with
/// a current OpenGL 4.5 context. `isSupported()` is the no-context-safe probe
/// for callers (it returns false when no context is current).
#pragma once

#include "physics/cloth_constraint_graph.h"
#include "physics/cloth_solver_backend.h"
#include "physics/cloth_simulator.h"  // For ClothConfig
#include "physics/cloth_wind_model.h"  // Shared gust + FBM/turbulence precompute (Sh4b)
#include "renderer/shader.h"

#include <glad/gl.h>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief GPU compute-shader cloth solver backend.
class GpuClothSimulator : public IClothSolverBackend
{
public:
    GpuClothSimulator();
    ~GpuClothSimulator() override;

    // Non-copyable — owns GL resources.
    GpuClothSimulator(const GpuClothSimulator&) = delete;
    GpuClothSimulator& operator=(const GpuClothSimulator&) = delete;

    // -- IClothSolverBackend --

    void initialize(const ClothConfig& config, uint32_t seed = 0) override;
    void simulate(float deltaTime) override;
    /// Phase 10.9 Cl2 — re-runs only the normals shader and flags the
    /// CPU mirror dirty. No integrate / wind / collide.
    void syncBuffersOnly() override;
    void reset() override;
    bool isInitialized() const override { return m_initialized; }
    uint32_t getParticleCount() const override { return m_particleCount; }
    const glm::vec3* getPositions() const override;
    const glm::vec3* getNormals() const override;
    const std::vector<uint32_t>& getIndices() const override { return m_indices; }
    const std::vector<glm::vec2>& getTexCoords() const override { return m_texCoords; }
    uint32_t getGridWidth() const override { return m_gridW; }
    uint32_t getGridHeight() const override { return m_gridH; }
    const ClothConfig& getConfig() const override { return m_config; }

    // Live-tuning setters (IClothSolverBackend).
    void setParticleMass(float mass) override;
    void setStretchCompliance(float compliance) override;
    void setShearCompliance(float compliance) override;
    void setBendCompliance(float compliance) override;

    /// @brief Phase 10.9 Cl4 — XPBD compliance for the dihedral bending
    ///        solver. Setter walks `m_dihedrals` updating each constraint's
    ///        `compliance` field then re-uploads the SSBO via
    ///        `glNamedBufferSubData`. Cheaper than rebuilding the dihedral
    ///        graph (which is the colour-grouped construction in
    ///        `buildAndUploadDihedrals`); compliance is the only field that
    ///        changes.
    void setDihedralBendCompliance(float compliance) override;
    float getDihedralBendCompliance() const override { return m_dihedralCompliance; }

    // Wind (IClothSolverBackend). Gust state + FBM/turbulence precompute live in
    // the shared ClothWindModel (Phase 10.9 Sh4b) so this backend produces the
    // same wind inputs as the CPU ClothSimulator from the same seed.
    void setWind(const glm::vec3& direction, float strength) override { m_windModel.setWind(direction, strength); }
    void setWindQuality(ClothWindQuality quality) override { m_windModel.setWindQuality(quality); }
    glm::vec3       getWindVelocity()  const override { return m_windModel.windVelocity(); }
    glm::vec3       getWindDirection() const override { return m_windModel.windDirection(); }
    float           getWindStrength()  const override { return m_windModel.windStrength(); }
    float           getDragCoefficient() const override { return m_windModel.dragCoefficient(); }
    ClothWindQuality getWindQuality()  const override { return m_windModel.windQuality(); }

    /// @brief Phase 10.9 Cl5 — refreshes the immutable rest-pose snapshot
    ///        used by `reset()`. Forces a SSBO→mirror readback first so the
    ///        snapshot reflects the *current* simulated state, not the
    ///        previous-frame mirror, then copies the mirror into
    ///        `m_initialPositions`. Mirrors `ClothSimulator::captureRestPositions`
    ///        at `cloth_simulator.cpp:714`. Pre-Cl5 this was a `{}` stub
    ///        and `reset()` re-uploaded the mutable mirror, so pinned
    ///        particles snapped to the last `setPinPosition` value rather
    ///        than the original grid.
    void captureRestPositions() override;

    // -- GPU backend extras --

    /// @brief Sets the directory containing the cloth_*.comp.glsl shaders.
    /// Must be called before `initialize()` for the GPU dispatch to engage;
    /// otherwise `simulate()` falls back to a no-op (the integration /
    /// wind shaders won't load).
    void setShaderPath(const std::string& path);

    /// @brief True once the wind + integrate compute shaders have loaded.
    /// Independent of `isInitialized()`: SSBOs may exist while shaders
    /// haven't loaded (e.g. asset path not set or shader compile failure),
    /// in which case `simulate()` cannot move particles.
    bool hasShaders() const { return m_shadersLoaded; }

    /// @brief Sets per-frame wind drag coefficient (matches CPU path).
    void setDragCoefficient(float drag) override { m_windModel.setDragCoefficient(drag); }

    /// @brief Sets a raw wind velocity vector. Decomposed into the model's
    ///        direction × strength so `getWindVelocity()` round-trips.
    void setWindVelocity(const glm::vec3& v) { m_windModel.setWind(v, glm::length(v)); }

    /// @brief Sets per-step velocity damping (0 = none, 0.99 = heavy).
    void setDamping(float damping) override { m_damping = damping; }

    /// @brief Probes the current GL context for compute-shader support.
    /// @return true if GL ≥ 4.3 with compute + SSBO; false otherwise (or no context).
    static bool isSupported();

    /// @brief SSBO / UBO binding indices match the contract in the design doc § 4.
    enum BufferBinding : GLuint
    {
        BIND_POSITIONS         = 0,
        BIND_PREV_POSITIONS    = 1,
        BIND_VELOCITIES        = 2,
        BIND_COLLIDERS_UBO     = 3,
        BIND_CONSTRAINTS       = 4,
        BIND_DIHEDRALS         = 5,
        BIND_NORMALS           = 6,
        BIND_INDICES           = 7,
        BIND_LRAS              = 8,
        BIND_TRIANGLES         = 9,  ///< Phase 10.9 Sh4a — per-triangle wind-drag records.
        BIND_PARTICLE_WIND_FBM = 10, ///< Phase 10.9 Sh4b — per-particle FBM perturbation (FULL).
        BIND_TRIANGLE_TURB     = 11, ///< Phase 10.9 Sh4b — per-triangle turbulence factor (FULL).
    };

    // -- Pin mutators (Step 9) --

    /// @brief Pins a particle to a fixed world-space position.
    /// @return true if pinned, false if @a index is out of bounds.
    bool pinParticle(uint32_t index, const glm::vec3& worldPos) override;

    /// @brief Unpins a particle (restores inverse mass).
    void unpinParticle(uint32_t index) override;

    /// @brief Moves an already-pinned particle to a new world-space position.
    void setPinPosition(uint32_t index, const glm::vec3& worldPos) override;

    /// @brief Returns true if the given particle is currently pinned.
    bool isParticlePinned(uint32_t index) const override;

    /// @brief Number of pinned particles.
    uint32_t getPinnedCount() const override { return static_cast<uint32_t>(m_pinIndices.size()); }

    /// @brief Rebuilds the LRA tether set from the current pin set + positions.
    /// Call after all pins are finalised. No-op if there are no pins.
    void rebuildLRA() override;

    /// @brief Number of LRA constraints currently active.
    uint32_t getLraCount() const { return m_lraCount; }

    // -- Collider mutators (Step 7) --

    /// @brief Adds a sphere collider. World-space center + positive radius.
    void addSphereCollider(const glm::vec3& center, float radius) override;

    /// @brief Removes all sphere colliders.
    void clearSphereColliders() override;

    /// @brief Adds a half-space plane collider. Normal auto-normalised; offset is along normal.
    /// @return true if added, false if normal was zero-length.
    bool addPlaneCollider(const glm::vec3& normal, float offset) override;

    /// @brief Removes all plane colliders.
    void clearPlaneColliders() override;

    /// @brief Sets the world-space Y of the ground plane. Particles stay at or above.
    void setGroundPlane(float worldY) override;

    /// @brief Returns the current ground plane Y.
    float getGroundPlane() const override { return m_groundY; }

    // Cylinder / box / mesh colliders are CPU-only (Phase 9B design doc § 9).
    // GPU backend logs a one-time warning and drops them so callers driving
    // the same code path don't have to special-case which backend is active.
    void addCylinderCollider(const glm::vec3& base, float radius, float height) override;
    void clearCylinderColliders() override;
    void addBoxCollider(const glm::vec3& min, const glm::vec3& max) override;
    void clearBoxColliders() override;

    /// @brief Sets the collision margin (default 0.015 m). Pushes particles `surface + margin`.
    void setCollisionMargin(float margin);

    /// @brief Returns the current collision margin.
    float getCollisionMargin() const { return m_collisionMargin; }

    /// @brief Phase 10.9 Sh3 — Coulomb friction coefficients uploaded to the
    /// Colliders UBO so `cloth_collision.comp.glsl` can apply tangential
    /// friction at every contact (ground / sphere / plane). Negative inputs
    /// clamp to zero. Defaults match the CPU `ClothSimulator` (0.4 / 0.3).
    void setFriction(float staticCoeff, float kineticCoeff) override;
    float getStaticFriction() const override { return m_staticFriction; }
    float getKineticFriction() const override { return m_kineticFriction; }

    /// @brief Number of sphere colliders currently active.
    uint32_t getSphereColliderCount() const { return static_cast<uint32_t>(m_sphereColliders.size()); }

    /// @brief Number of plane colliders currently active.
    uint32_t getPlaneColliderCount() const { return static_cast<uint32_t>(m_planeColliders.size()); }

    GLuint getPositionsSSBO() const { return m_positionsSSBO; }
    GLuint getPrevPositionsSSBO() const { return m_prevPositionsSSBO; }
    GLuint getVelocitiesSSBO() const { return m_velocitiesSSBO; }
    GLuint getConstraintsSSBO() const { return m_constraintsSSBO; }
    GLuint getDihedralsSSBO() const { return m_dihedralsSSBO; }
    GLuint getNormalsSSBO() const { return m_normalsSSBO; }
    GLuint getIndicesSSBO()   const { return m_indicesSSBO; }
    GLuint getTrianglesSSBO() const { return m_trianglesSSBO; }  ///< Phase 10.9 Sh4a.

    /// @brief Number of distance constraints (stretch + shear + bend) generated by initialize().
    uint32_t getConstraintCount() const override { return m_constraintCount; }

    /// @brief Number of dihedral bending constraints generated by initialize().
    uint32_t getDihedralCount() const { return m_dihedralCount; }

    /// @brief Number of colours produced by greedy graph colouring of the distance constraints.
    uint32_t getColourCount() const { return static_cast<uint32_t>(m_colourRanges.size()); }

    /// @brief Number of colours produced by greedy colouring of the dihedral constraint graph.
    uint32_t getDihedralColourCount() const { return static_cast<uint32_t>(m_dihedralColourRanges.size()); }

    /// @brief Phase 10.9 Sh4a — number of colour groups from greedy triangle
    ///        colouring. Each colour is one dispatch in the per-triangle drag pass.
    uint32_t getTriangleColourCount() const { return static_cast<uint32_t>(m_triangleColourRanges.size()); }

    /// @brief Number of XPBD substeps per simulate() call (default 10).
    void setSubsteps(int substeps) override;
    int  getSubsteps() const { return m_substeps; }

private:
    void createBuffers();
    void destroyBuffers();
    void buildInitialGrid(const ClothConfig& config);
    void buildAndUploadConstraints(const ClothConfig& config);
    void buildAndUploadDihedrals(const ClothConfig& config);
    void buildAndUploadTriangles(); ///< Phase 10.9 Sh4a — triangle colour-groups for wind drag.
    void uploadCollidersIfDirty();
    void uploadPinsIfDirty();
    void loadShadersIfNeeded();
    void readbackPositionsIfDirty() const;
    void readbackNormalsIfDirty() const;
    /// Phase 10.9 Cl2 — dispatch only the cloth_normals compute shader.
    /// Shared by `simulate()` and `syncBuffersOnly()`.
    void dispatchNormalsShader(GLuint particleGroups);

    bool m_initialized = false;
    uint32_t m_particleCount = 0;
    uint32_t m_gridW = 0;
    uint32_t m_gridH = 0;

    // CPU mirror. Step 3 onward: marked dirty whenever `simulate()` runs;
    // `getPositions()` / `getNormals()` lazily refresh via glGetNamedBufferSubData
    // before returning. Once Step 8 wires the renderer to read SSBOs directly,
    // most frames will skip the readback entirely.
    mutable std::vector<glm::vec3> m_positionMirror;
    mutable std::vector<glm::vec3> m_normalMirror;
    mutable bool m_positionsDirty = false;
    mutable bool m_normalsDirty   = false;
    std::vector<uint32_t> m_indices;
    std::vector<glm::vec2> m_texCoords;
    std::vector<float>    m_invMassMirror;  ///< 1 = free, 0 = pinned. Source of truth for pin state.

    /// @brief Phase 10.9 Cl5 — immutable rest-pose snapshot consumed by
    ///        `reset()`. Captured at the end of `buildInitialGrid()` and
    ///        refreshable via `captureRestPositions()`. Decouples reset
    ///        from `m_positionMirror`, which `setPinPosition` and
    ///        `readbackPositionsIfDirty` both mutate. Mirrors the CPU
    ///        backend's `m_initialPositions` at `cloth_simulator.h:344`.
    std::vector<glm::vec3> m_initialPositions;

    // SSBOs (named to mirror BufferBinding enum).
    GLuint m_positionsSSBO     = 0;
    GLuint m_prevPositionsSSBO = 0;
    GLuint m_velocitiesSSBO    = 0;
    GLuint m_constraintsSSBO   = 0;
    GLuint m_dihedralsSSBO     = 0;
    GLuint m_normalsSSBO       = 0;
    GLuint m_indicesSSBO       = 0;
    GLuint m_collidersUBO      = 0;
    GLuint m_lraSSBO           = 0;

    // Distance constraint graph + colouring.
    std::vector<GpuConstraint> m_constraints;
    std::vector<ColourRange>   m_colourRanges;
    uint32_t                    m_constraintCount = 0;

    // Dihedral constraint graph + colouring.
    std::vector<GpuDihedralConstraint> m_dihedrals;
    std::vector<ColourRange>           m_dihedralColourRanges;
    uint32_t                            m_dihedralCount = 0;

    // Phase 10.9 Sh4a — triangle wind-drag records + colouring.
    std::vector<GpuTriangle>   m_triangles;
    std::vector<ColourRange>   m_triangleColourRanges;
    uint32_t                    m_triangleCount = 0;
    GLuint                      m_trianglesSSBO = 0;

    // Phase 10.9 Sh4b — FULL-tier wind: per-particle FBM + per-triangle
    // turbulence. The shared wind model fills the CPU-side caches each frame;
    // they upload to these SSBOs (consumed every substep). `m_fbmUploadScratch`
    // packs the model's vec3 perturbation into std430 vec4 without a per-frame
    // allocation.
    GLuint                      m_particleWindFbmSSBO = 0;
    GLuint                      m_triangleTurbSSBO    = 0;
    std::vector<glm::vec4>      m_fbmUploadScratch;

    // Compute shaders.
    Shader m_windShader;
    Shader m_windDragShader;  ///< Phase 10.9 Sh4a — per-triangle aerodynamic drag.
    Shader m_windFbmShader;   ///< Phase 10.9 Sh4b — per-particle FBM perturbation (FULL).
    Shader m_integrateShader;
    Shader m_constraintsShader;
    Shader m_dihedralShader;
    Shader m_collisionShader;
    Shader m_normalsShader;
    Shader m_lraShader;
    bool m_shadersLoaded = false;
    std::string m_shaderPath;

    // Collider state (CPU-mirror; uploaded to UBO when dirty).
    static constexpr int MAX_GPU_SPHERE_COLLIDERS = 32;
    static constexpr int MAX_GPU_PLANE_COLLIDERS  = 16;
    std::vector<glm::vec4> m_sphereColliders;   // xyz=center, w=radius
    std::vector<glm::vec4> m_planeColliders;    // xyz=normal, w=offset
    float m_groundY         = -1000.0f;
    float m_collisionMargin = 0.015f;
    float m_staticFriction  = 0.4f;   ///< Phase 10.9 Sh3 — matches CPU default.
    float m_kineticFriction = 0.3f;   ///< Phase 10.9 Sh3 — matches CPU default.
    bool  m_collidersDirty  = true;

    // Pin + LRA state (Step 9). Pin set is tracked as a sorted unique vector
    // of particle indices. The CPU position mirror's `w` channel is the
    // single source of truth for pinned-vs-free; the GPU positions SSBO is
    // re-uploaded when pin state changes.
    std::vector<uint32_t>           m_pinIndices;
    std::vector<GpuLraConstraint>   m_lras;
    uint32_t                         m_lraCount      = 0;
    bool                             m_pinsDirty     = false;

    // Per-frame parameters (uniforms uploaded inside simulate()).
    glm::vec3 m_gravity      = glm::vec3(0.0f, -9.81f, 0.0f);
    float     m_damping      = 0.01f;

    // Wind state machine + FBM/turbulence precompute, shared with the CPU
    // backend so both produce identical wind inputs from the same seed (Sh4b).
    ClothWindModel m_windModel;
    /// @brief Phase 10.9 Cl4 — current dihedral compliance, mirroring the
    /// CPU `m_dihedralCompliance` default. Initial value picked at
    /// `buildAndUploadDihedrals`; runtime changes go through
    /// `setDihedralBendCompliance` which re-uploads the SSBO.
    float     m_dihedralCompliance = 0.01f;
    int       m_substeps     = 10;        ///< XPBD substeps per simulate(); matches CPU default.

    // Cached ClothConfig returned by getConfig(); updated when live-tuning
    // setters mutate the canonical values. Lives here so `getConfig()` can
    // return a stable reference without reconstructing.
    ClothConfig m_config;

    // When any live compliance setter moves, set this true so the next
    // `simulate()` call re-uploads the constraints SSBO. Keeps live tuning
    // cheap for the 95% case (values don't change) while correct for the 5%.
    bool m_compliancesDirty = false;
};

} // namespace Vestige
