// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_particle_system.h
/// @brief GPU compute shader particle pipeline with SSBO management and indirect draw.
#pragma once

#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Vestige
{

/// @brief Composable particle behavior types (mapped to GPU constants).
enum class ParticleBehaviorType : uint32_t
{
    GRAVITY          = 0,
    DRAG             = 1,
    NOISE            = 2,
    ORBIT            = 3,
    ATTRACT          = 4,
    VORTEX           = 5,
    TURBULENCE       = 6,
    WIND             = 7,

    DEPTH_COLLISION  = 10,
    GROUND_PLANE     = 11,
    SPHERE_COLLIDER  = 12,

    KILL_ON_COLLISION = 20
};

/// @brief Single behavior entry uploaded to GPU uniform buffer.
struct BehaviorEntry
{
    ParticleBehaviorType type = ParticleBehaviorType::GRAVITY;
    uint32_t flags = 0;
    float params[6] = {};
};

/// @brief Over-lifetime curve and behavior data uploaded to GPU each frame.
struct BehaviorBlock
{
    BehaviorEntry behaviors[16] = {};
    int behaviorCount = 0;

    // Color gradient (up to 8 stops)
    glm::vec4 colorStops[8] = {};
    float colorStopTimes[8] = {};
    int colorStopCount = 0;

    // Size over lifetime (up to 8 keyframes)
    float sizeKeys[8] = {};
    float sizeKeyTimes[8] = {};
    int sizeKeyCount = 0;

    // Speed over lifetime (up to 8 keyframes)
    float speedKeys[8] = {};
    float speedKeyTimes[8] = {};
    int speedKeyCount = 0;
};

/// @brief Emission parameters sent to the GPU each frame.
struct EmissionParams
{
    glm::vec3 worldPosition = glm::vec3(0.0f);
    glm::mat3 worldRotation = glm::mat3(1.0f);
    uint32_t shapeType = 0;            ///< 0=point, 1=sphere, 2=cone, 3=box, 4=ring
    float shapeRadius = 1.0f;
    float shapeConeAngle = 0.4363f;    ///< 25 degrees in radians
    glm::vec3 shapeBoxSize = glm::vec3(1.0f);
    float startLifetimeMin = 1.0f;
    float startLifetimeMax = 3.0f;
    float startSpeedMin = 1.0f;
    float startSpeedMax = 3.0f;
    float startSizeMin = 0.1f;
    float startSizeMax = 0.5f;
    glm::vec4 startColor = glm::vec4(1.0f);
    uint32_t randomSeed = 0;
};

/// @brief GPU compute shader particle pipeline.
///
/// Manages particle data in SSBOs, dispatches compute shaders for emission,
/// simulation, compaction, sorting, and indirect draw command updates.
/// Uses ping-pong buffers for read-write safety.
///
/// Pipeline per frame: emit → simulate → compact → (sort) → update indirect → render
class GPUParticleSystem
{
public:
    GPUParticleSystem();
    ~GPUParticleSystem();

    // Non-copyable
    GPUParticleSystem(const GPUParticleSystem&) = delete;
    GPUParticleSystem& operator=(const GPUParticleSystem&) = delete;

    /// @brief Initializes GPU buffers and loads compute shaders.
    /// @param shaderPath Base path to the shaders directory.
    /// @param maxParticles Maximum number of particles in the pool.
    /// @return True if all shaders loaded and buffers created successfully.
    bool init(const std::string& shaderPath, uint32_t maxParticles);

    /// @brief Cleans up all GPU resources.
    void shutdown();

    /// @brief Returns true if init() succeeded.
    bool isInitialized() const;

    /// @brief Returns the maximum particle capacity.
    uint32_t getMaxParticles() const;

    // --- Per-frame pipeline ---

    /// @brief Resets counters for a new frame (called before emit/simulate).
    void beginFrame();

    /// @brief Dispatches the emission compute shader.
    /// @param count Number of particles to spawn this frame.
    /// @param params Emission shape and property parameters.
    void emit(uint32_t count, const EmissionParams& params);

    /// @brief Dispatches the simulation compute shader.
    /// @param deltaTime Frame time in seconds.
    /// @param behaviors Composable behavior data.
    /// @param elapsed Total elapsed time (for noise animation).
    void simulate(float deltaTime, const BehaviorBlock& behaviors, float elapsed);

    /// @brief Dispatches stream compaction to remove dead particles.
    void compact();

    /// @brief Dispatches bitonic sort for back-to-front transparency.
    /// @param viewMatrix Camera view matrix (for depth calculation).
    void sort(const glm::mat4& viewMatrix);

    /// @brief Dispatches indirect draw command update (alive count → instance count).
    void updateIndirectCommand();

    // --- Depth buffer collision ---

    /// @brief Enables depth buffer collision for the simulation pass.
    /// @param depthTexture Scene depth texture handle.
    /// @param viewProjection Current view-projection matrix.
    /// @param screenSize Viewport dimensions.
    /// @param cameraNear Near plane distance.
    void setDepthCollision(GLuint depthTexture, const glm::mat4& viewProjection,
                           const glm::vec2& screenSize, float cameraNear);

    /// @brief Disables depth buffer collision.
    void clearDepthCollision();

    // --- Rendering ---

    /// @brief Binds particle SSBO and indirect buffer for rendering.
    void bindForRendering() const;

    /// @brief Issues glDrawArraysIndirect to render particles.
    void drawIndirect() const;

    /// @brief Returns the particle SSBO handle (for shader binding).
    GLuint getParticleSSBO() const;

    /// @brief Returns the indirect draw command buffer handle.
    GLuint getIndirectBuffer() const;

    /// @brief Returns the sort key buffer handle (for sorted rendering).
    GLuint getSortKeySSBO() const;

    // --- Debug ---

    /// @brief Reads alive count from GPU (causes sync stall — debug only).
    uint32_t readAliveCount() const;

    /// @brief Reads dead count from GPU (causes sync stall — debug only).
    uint32_t readDeadCount() const;

private:
    void createBuffers(uint32_t maxParticles);
    void initializeFreeList(uint32_t maxParticles);
    void uploadBehaviorBlock(const BehaviorBlock& behaviors);
    uint32_t nextPowerOf2(uint32_t n) const;

    bool m_initialized = false;
    uint32_t m_maxParticles = 0;
    uint32_t m_frameCount = 0;

    // Compute shaders
    Shader m_emitShader;
    Shader m_simulateShader;
    Shader m_compactShader;
    Shader m_sortShader;
    Shader m_indirectShader;

    // SSBOs
    GLuint m_particleSSBO = 0;       ///< Particle data (binding 0)
    GLuint m_counterSSBO = 0;        ///< aliveCount, deadCount, emitCount, maxParticles (binding 1)
    GLuint m_freeListSSBO = 0;       ///< Free slot indices (binding 2)
    GLuint m_indirectDrawSSBO = 0;   ///< DrawArraysIndirectCommand (binding 3)
    GLuint m_behaviorUBO = 0;        ///< BehaviorBlock uniform buffer (binding 4)
    GLuint m_sortKeySSBO = 0;        ///< Sort key pairs (binding 5)

    // Depth collision state
    bool m_depthCollisionEnabled = false;
    GLuint m_depthTexture = 0;
    glm::mat4 m_depthVP = glm::mat4(1.0f);
    glm::vec2 m_screenSize = glm::vec2(0.0f);
    float m_cameraNear = 0.1f;
};

} // namespace Vestige
