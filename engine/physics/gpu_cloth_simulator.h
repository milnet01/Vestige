// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_cloth_simulator.h
/// @brief GPU compute-shader backend for cloth simulation (Phase 9B skeleton).
///
/// Implements `IClothSolverBackend` using SSBOs for particle state. The
/// per-substep work (force accumulation, integration, constraint solving,
/// collision, normal recomputation) lands incrementally over Steps 3–9 of
/// the Phase 9B GPU cloth pipeline — see `docs/PHASE9B_GPU_CLOTH_DESIGN.md`.
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

    // Wind (IClothSolverBackend).
    void setWind(const glm::vec3& direction, float strength) override;
    void setWindQuality(ClothWindQuality quality) override { m_windQuality = quality; }
    glm::vec3       getWindVelocity()  const override { return m_windVelocity; }
    glm::vec3       getWindDirection() const override;
    float           getWindStrength()  const override;
    float           getDragCoefficient() const override { return m_dragCoeff; }
    ClothWindQuality getWindQuality()  const override { return m_windQuality; }

    // Rest-pose snapshot. The GPU backend tracks rest pose implicitly via the
    // mirror built at initialize() time; after pin reconfiguration the
    // position mirror already reflects the new rest. Stubbed as a no-op —
    // a future Phase 9B extension could force a GPU→CPU readback into a
    // dedicated m_restPose array, but nothing calls that today.
    void captureRestPositions() override {}

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
    void setDragCoefficient(float drag) override { m_dragCoeff = drag; }

    /// @brief Sets uniform wind velocity (direction × strength).
    void setWindVelocity(const glm::vec3& v) { m_windVelocity = v; }

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
    GLuint getIndicesSSBO() const { return m_indicesSSBO; }

    /// @brief Number of distance constraints (stretch + shear + bend) generated by initialize().
    uint32_t getConstraintCount() const override { return m_constraintCount; }

    /// @brief Number of dihedral bending constraints generated by initialize().
    uint32_t getDihedralCount() const { return m_dihedralCount; }

    /// @brief Number of colours produced by greedy graph colouring of the distance constraints.
    uint32_t getColourCount() const { return static_cast<uint32_t>(m_colourRanges.size()); }

    /// @brief Number of colours produced by greedy colouring of the dihedral constraint graph.
    uint32_t getDihedralColourCount() const { return static_cast<uint32_t>(m_dihedralColourRanges.size()); }

    /// @brief Number of XPBD substeps per simulate() call (default 10).
    void setSubsteps(int substeps) override;
    int  getSubsteps() const { return m_substeps; }

private:
    void createBuffers();
    void destroyBuffers();
    void buildInitialGrid(const ClothConfig& config);
    void buildAndUploadConstraints(const ClothConfig& config);
    void buildAndUploadDihedrals(const ClothConfig& config);
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

    // Compute shaders.
    Shader m_windShader;
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
    glm::vec3 m_windVelocity = glm::vec3(0.0f);
    float     m_dragCoeff    = 1.0f;
    float     m_damping      = 0.01f;
    int       m_substeps     = 10;        ///< XPBD substeps per simulate(); matches CPU default.
    ClothWindQuality m_windQuality = ClothWindQuality::FULL;

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
