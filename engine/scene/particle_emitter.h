/// @file particle_emitter.h
/// @brief Particle emitter component with SoA data container and CPU simulation.
#pragma once

#include "scene/component.h"
#include "editor/widgets/animation_curve.h"
#include "editor/widgets/color_gradient.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Emitter configuration — all tweakable parameters for a particle system.
struct ParticleEmitterConfig
{
    // Emission
    float emissionRate = 10.0f;          ///< Particles spawned per second
    int maxParticles = 1000;             ///< Pool capacity
    bool looping = true;                 ///< Restart after duration
    float duration = 5.0f;              ///< System duration in seconds (if not looping)

    // Start properties (randomized between min and max)
    float startLifetimeMin = 1.0f;
    float startLifetimeMax = 3.0f;
    float startSpeedMin = 1.0f;
    float startSpeedMax = 3.0f;
    float startSizeMin = 0.1f;
    float startSizeMax = 0.5f;
    glm::vec4 startColor = {1.0f, 1.0f, 1.0f, 1.0f};

    // Forces
    glm::vec3 gravity = {0.0f, -9.81f, 0.0f};

    // Emission shape
    enum class Shape
    {
        POINT,
        SPHERE,
        CONE,
        BOX
    };
    Shape shape = Shape::POINT;
    float shapeRadius = 1.0f;            ///< Sphere/cone radius
    float shapeConeAngle = 25.0f;        ///< Half-angle in degrees
    glm::vec3 shapeBoxSize = {1.0f, 1.0f, 1.0f};

    // Over-lifetime modifiers
    bool useColorOverLifetime = false;
    ColorGradient colorOverLifetime;      ///< RGBA gradient sampled by normalized age

    bool useSizeOverLifetime = false;
    AnimationCurve sizeOverLifetime;      ///< Multiplier on startSize (default: 1→0 fade-out)

    bool useSpeedOverLifetime = false;
    AnimationCurve speedOverLifetime;     ///< Multiplier on velocity magnitude

    // Rendering
    enum class BlendMode
    {
        ADDITIVE,
        ALPHA_BLEND
    };
    BlendMode blendMode = BlendMode::ADDITIVE;
    std::string texturePath;              ///< Optional particle texture
};

/// @brief SoA (Structure of Arrays) particle data container.
///
/// Uses swap-on-death compaction: when a particle dies, it is swapped with
/// the last live particle, keeping all live particles contiguous in memory.
struct ParticleData
{
    int count = 0;       ///< Number of live particles
    int maxCount = 0;    ///< Allocated capacity

    // SoA arrays — pre-allocated to maxCount
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> velocities;
    std::vector<glm::vec4> colors;
    std::vector<float> sizes;
    std::vector<float> startSizes;     ///< Initial size (for over-lifetime multiplier)
    std::vector<float> startSpeeds;    ///< Initial speed magnitude (for over-lifetime multiplier)
    std::vector<float> ages;
    std::vector<float> lifetimes;

    /// @brief Allocates all arrays to the given capacity. Resets count to 0.
    void resize(int max);

    /// @brief Kills particle at index by swapping with the last live particle.
    void kill(int index);

    /// @brief Resets all particles.
    void clear();
};

/// @brief Entity component that manages a particle emitter and its simulation.
class ParticleEmitterComponent : public Component
{
public:
    ParticleEmitterComponent();

    /// @brief Per-frame simulation: spawn new particles, update existing, kill expired.
    /// @param deltaTime Frame time in seconds.
    void update(float deltaTime) override;

    /// @brief Creates a deep copy for entity duplication.
    std::unique_ptr<Component> clone() const override;

    /// @brief Gets the emitter configuration (for editor / serialization).
    ParticleEmitterConfig& getConfig();
    const ParticleEmitterConfig& getConfig() const;

    /// @brief Gets the live particle data (for rendering).
    const ParticleData& getData() const;

    /// @brief Resets the emitter: kills all particles and restarts the timer.
    void restart();

    /// @brief Pauses/resumes simulation.
    void setPaused(bool paused);
    bool isPaused() const;

    /// @brief Returns true if the emitter is currently active (playing and within duration).
    bool isPlaying() const;

private:
    void spawnParticle(const glm::mat4& worldMatrix);
    glm::vec3 generateSpawnOffset() const;
    glm::vec3 generateSpawnDirection() const;

    ParticleEmitterConfig m_config;
    ParticleData m_data;

    float m_emitAccumulator = 0.0f;   ///< Fractional particle accumulator
    float m_elapsedTime = 0.0f;       ///< Total time since start/restart
    bool m_paused = false;
};

} // namespace Vestige
