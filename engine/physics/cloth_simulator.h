/// @file cloth_simulator.h
/// @brief CPU-based cloth simulation using XPBD (Extended Position-Based Dynamics).
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

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
    float damping = 0.01f;             ///< Velocity damping per substep
    glm::vec3 gravity = {0, -9.81f, 0};
};

/// @brief A sphere collider for cloth collision.
struct ClothSphereCollider
{
    glm::vec3 center;
    float radius;
};

/// @brief Pure-CPU cloth simulator. No OpenGL or entity dependencies.
///
/// Generates a rectangular grid of particles connected by distance constraints
/// (structural, shear, bending). Uses XPBD for iteration-count-independent
/// stiffness. Supports pin constraints, sphere/plane collision, and wind.
class ClothSimulator
{
public:
    /// @brief Initializes the cloth grid, particles, and constraints.
    /// @param seed Unique seed for wind randomness (different seed = different timing).
    void initialize(const ClothConfig& config, uint32_t seed = 0);

    /// @brief Advances the simulation by deltaTime seconds.
    void simulate(float deltaTime);

    /// @brief Returns the total number of particles.
    uint32_t getParticleCount() const;

    /// @brief Returns a pointer to the particle positions array.
    const glm::vec3* getPositions() const;

    /// @brief Returns a pointer to the per-vertex normals (recomputed after simulate).
    const glm::vec3* getNormals() const;

    /// @brief Returns the grid width (particles along X).
    uint32_t getGridWidth() const;

    /// @brief Returns the grid height (particles along Z).
    uint32_t getGridHeight() const;

    /// @brief Returns the triangle index buffer for rendering.
    const std::vector<uint32_t>& getIndices() const;

    /// @brief Returns the UV coordinates for each particle.
    const std::vector<glm::vec2>& getTexCoords() const;

    // --- Pin constraints ---

    /// @brief Pins a particle to a fixed world-space position (inverse mass → 0).
    void pinParticle(uint32_t index, const glm::vec3& worldPos);

    /// @brief Unpins a particle, restoring its original mass.
    void unpinParticle(uint32_t index);

    /// @brief Moves a pinned particle to a new position.
    void setPinPosition(uint32_t index, const glm::vec3& worldPos);

    /// @brief Returns true if the given particle is pinned.
    bool isParticlePinned(uint32_t index) const;

    // --- Collision ---

    /// @brief Adds a sphere collider for the cloth to collide against.
    void addSphereCollider(const glm::vec3& center, float radius);

    /// @brief Removes all sphere colliders.
    void clearSphereColliders();

    /// @brief Sets the ground plane height (particles are pushed above this Y).
    void setGroundPlane(float height);

    /// @brief Returns the current ground plane height.
    float getGroundPlane() const;

    // --- Wind ---

    /// @brief Sets the wind direction and strength.
    void setWind(const glm::vec3& direction, float strength);

    /// @brief Sets the aerodynamic drag coefficient (default 1.0).
    void setDragCoefficient(float drag);

    /// @brief Returns the current wind velocity (direction * strength).
    glm::vec3 getWindVelocity() const;

    // --- Config ---

    /// @brief Updates the substep count.
    void setSubsteps(int substeps);

    /// @brief Returns the current configuration.
    const ClothConfig& getConfig() const;

    /// @brief Returns true if initialize() has been called.
    bool isInitialized() const;

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

    // Pin constraints
    struct PinConstraint
    {
        uint32_t index;
        glm::vec3 position;
    };
    std::vector<PinConstraint> m_pinConstraints;

    // Collision primitives
    std::vector<ClothSphereCollider> m_sphereColliders;
    float m_groundPlaneY = -1000.0f;

    // Wind
    glm::vec3 m_windDirection = glm::vec3(0.0f);
    float m_windStrength = 0.0f;
    float m_dragCoeff = 1.0f;
    float m_elapsed = 0.0f;

    // Gust state machine: creates realistic blow/calm cycles
    float m_gustCurrent = 0.0f;       ///< Current gust intensity [0,1]
    float m_gustTarget = 0.0f;        ///< Target gust intensity
    float m_gustTimer = 0.0f;         ///< Time until next target change
    float m_gustRampSpeed = 0.0f;     ///< How fast to reach target
    glm::vec3 m_windDirOffset = glm::vec3(0.0f);  ///< Current direction offset
    glm::vec3 m_windDirTarget = glm::vec3(0.0f);  ///< Target direction offset
    float m_dirTimer = 0.0f;          ///< Time until next direction change
    uint32_t m_rngState = 12345u;     ///< Simple LCG state for deterministic randomness

    float randFloat();                ///< Returns [0, 1) from internal RNG
    float randRange(float lo, float hi);
    void updateGustState(float dt);

    // Grid topology
    uint32_t m_gridW = 0;
    uint32_t m_gridH = 0;
    std::vector<uint32_t> m_indices;

    // Solver internals
    void solveDistanceConstraint(DistanceConstraint& c, float alphaTilde);
    void solvePinConstraints();
    void applyCollisions();
    void recomputeNormals();
    void applyWind(float dt);
};

} // namespace Vestige
