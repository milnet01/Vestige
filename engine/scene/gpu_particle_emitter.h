// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_particle_emitter.h
/// @brief GPU-accelerated particle emitter with composable behavior system.
///
/// Inspired by Unreal Engine Niagara's module composition pattern but with our own
/// naming and architecture. Uses compute shaders for simulation, supporting 100k+
/// particles without CPU bottleneck.
///
/// Behaviors (forces, modifiers, collision) are composed freely to build particle effects.
/// Same ParticleEmitterConfig format as the CPU path for interoperability.
#pragma once

#include "scene/component.h"
#include "scene/particle_emitter.h"
#include "renderer/gpu_particle_system.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Parameters for a single composable behavior.
struct BehaviorParams
{
    float values[6] = {};
};

/// @brief GPU-accelerated particle emitter component.
///
/// Uses compute shaders for simulation, achieving 10-50x performance over the CPU
/// path for large particle counts. Supports composable behaviors (forces, collision,
/// lifetime modifiers) that are uploaded to the GPU as a uniform buffer each frame.
///
/// Automatically manages a GPUParticleSystem instance. Falls back gracefully if
/// GPU compute is unavailable.
class GPUParticleEmitter : public Component
{
public:
    GPUParticleEmitter();
    ~GPUParticleEmitter() override;

    /// @brief Initializes the GPU particle system.
    /// @param shaderPath Base path to shaders directory.
    /// @return True if GPU resources allocated successfully.
    bool init(const std::string& shaderPath);

    /// @brief Per-frame update: handles emission timing and dispatches GPU pipeline.
    void update(float deltaTime) override;

    /// @brief Creates a deep copy for entity duplication.
    std::unique_ptr<Component> clone() const override;

    // --- Configuration ---

    /// @brief Sets the particle emitter configuration.
    void setConfig(const ParticleEmitterConfig& config);

    /// @brief Gets the particle emitter configuration.
    ParticleEmitterConfig& getConfig();
    const ParticleEmitterConfig& getConfig() const;

    // --- Behavior composition ---

    /// @brief Adds a composable behavior to the particle system.
    /// @param type The behavior type (gravity, drag, noise, etc.).
    /// @param params Type-specific parameters.
    void addBehavior(ParticleBehaviorType type, const BehaviorParams& params);

    /// @brief Removes all behaviors of the given type.
    void removeBehavior(ParticleBehaviorType type);

    /// @brief Removes all behaviors.
    void clearBehaviors();

    /// @brief Returns the number of active behaviors.
    int getBehaviorCount() const;

    // --- State ---

    /// @brief Pauses/resumes simulation.
    void setPaused(bool paused);
    bool isPaused() const;

    /// @brief Resets the emitter: kills all particles and restarts.
    void restart();

    /// @brief Returns true if the emitter is actively simulating.
    bool isPlaying() const;

    /// @brief Returns the approximate alive particle count (1-frame delayed).
    uint32_t getAliveCount() const;

    /// @brief Returns true if using GPU compute path.
    bool isGPUPath() const;

    // --- Rendering access ---

    /// @brief Gets the underlying GPU particle system (for renderer).
    GPUParticleSystem* getGPUSystem();
    const GPUParticleSystem* getGPUSystem() const;

    /// @brief Returns the blend mode for rendering.
    ParticleEmitterConfig::BlendMode getBlendMode() const;

    /// @brief Returns the texture path for rendering.
    const std::string& getTexturePath() const;

    /// @brief Returns true if this layer needs sorting (ALPHA_BLEND mode).
    bool needsSorting() const;

    // --- Depth collision ---

    /// @brief Enables depth buffer collision detection.
    void enableDepthCollision(GLuint depthTexture, const glm::mat4& viewProjection,
                              const glm::vec2& screenSize, float cameraNear);

    /// @brief Disables depth buffer collision.
    void disableDepthCollision();

    // --- Config from ParticleEmitterConfig helpers ---

    /// @brief Builds the behavior list from a ParticleEmitterConfig.
    /// Converts gravity, over-lifetime curves, etc. into the composable behavior system.
    void buildBehaviorsFromConfig();

private:
    void buildBehaviorBlock();
    EmissionParams buildEmissionParams() const;

    ParticleEmitterConfig m_config;
    std::unique_ptr<GPUParticleSystem> m_gpuSystem;
    bool m_gpuInitialized = false;

    // Behavior list
    struct BehaviorSlot
    {
        ParticleBehaviorType type;
        BehaviorParams params;
    };
    std::vector<BehaviorSlot> m_behaviors;
    BehaviorBlock m_behaviorBlock;
    bool m_behaviorsDirty = true;

    // Emission state
    float m_emitAccumulator = 0.0f;
    float m_elapsedTime = 0.0f;
    bool m_paused = false;

    // Cached alive count (from previous frame, avoids GPU stall)
    mutable uint32_t m_cachedAliveCount = 0;

    std::string m_shaderPath;
};

} // namespace Vestige
