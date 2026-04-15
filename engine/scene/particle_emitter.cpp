// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_emitter.cpp
/// @brief Particle emitter CPU simulation.
#include "scene/particle_emitter.h"
#include "scene/entity.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Thread-local RNG for particle randomization
// ---------------------------------------------------------------------------

static thread_local std::mt19937 s_rng(std::random_device{}());

static float randomFloat(float minVal, float maxVal)
{
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(s_rng);
}

static glm::vec3 randomUnitSphere()
{
    // Rejection sampling for uniform sphere
    glm::vec3 v;
    do
    {
        v.x = randomFloat(-1.0f, 1.0f);
        v.y = randomFloat(-1.0f, 1.0f);
        v.z = randomFloat(-1.0f, 1.0f);
    } while (glm::dot(v, v) > 1.0f || glm::dot(v, v) < 0.0001f);
    return glm::normalize(v);
}

// ---------------------------------------------------------------------------
// ParticleData
// ---------------------------------------------------------------------------

void ParticleData::resize(int max)
{
    maxCount = max;
    // Preserve live particles up to the new capacity (don't reset count to 0)
    if (count > max)
    {
        count = max;
    }
    positions.resize(static_cast<size_t>(max));
    velocities.resize(static_cast<size_t>(max));
    colors.resize(static_cast<size_t>(max));
    sizes.resize(static_cast<size_t>(max));
    startSizes.resize(static_cast<size_t>(max));
    startSpeeds.resize(static_cast<size_t>(max));
    ages.resize(static_cast<size_t>(max));
    lifetimes.resize(static_cast<size_t>(max));
    normalizedAges.resize(static_cast<size_t>(max));
}

void ParticleData::kill(int index)
{
    if (index < 0 || index >= count)
    {
        return;
    }

    // Swap with last live particle
    int last = count - 1;
    if (index != last)
    {
        positions[index] = positions[last];
        velocities[index] = velocities[last];
        colors[index] = colors[last];
        sizes[index] = sizes[last];
        startSizes[index] = startSizes[last];
        startSpeeds[index] = startSpeeds[last];
        ages[index] = ages[last];
        lifetimes[index] = lifetimes[last];
        normalizedAges[index] = normalizedAges[last];
    }
    --count;
}

void ParticleData::clear()
{
    count = 0;
}

// ---------------------------------------------------------------------------
// ParticleEmitterComponent
// ---------------------------------------------------------------------------

ParticleEmitterComponent::ParticleEmitterComponent()
{
    m_data.resize(m_config.maxParticles);
}

void ParticleEmitterComponent::update(float deltaTime)
{
    if (m_paused || !isEnabled())
    {
        return;
    }

    m_elapsedTime += deltaTime;

    // Check duration (only for non-looping systems)
    bool canEmit = m_config.looping || (m_elapsedTime < m_config.duration);

    // Resize pool if config changed (resize preserves live particles)
    if (m_data.maxCount != m_config.maxParticles)
    {
        m_data.resize(m_config.maxParticles);
    }

    // Get world matrix from owning entity (for spawn position)
    glm::mat4 worldMatrix(1.0f);
    if (m_owner)
    {
        worldMatrix = m_owner->getWorldMatrix();
    }

    // --- Spawn new particles ---
    if (canEmit && m_config.emissionRate > 0.0f)
    {
        m_emitAccumulator += m_config.emissionRate * deltaTime;
        int toSpawn = static_cast<int>(m_emitAccumulator);
        m_emitAccumulator -= static_cast<float>(toSpawn);

        for (int i = 0; i < toSpawn && m_data.count < m_data.maxCount; ++i)
        {
            spawnParticle(worldMatrix);
        }
    }

    // --- Update existing particles ---
    for (int i = m_data.count - 1; i >= 0; --i)
    {
        m_data.ages[i] += deltaTime;

        // Kill expired
        if (m_data.ages[i] >= m_data.lifetimes[i])
        {
            m_data.kill(i);
            continue;
        }

        float normalizedAge = m_data.ages[i] / m_data.lifetimes[i];
        m_data.normalizedAges[i] = normalizedAge;

        // Apply gravity
        m_data.velocities[i] += m_config.gravity * deltaTime;

        // Over-lifetime: color
        if (m_config.useColorOverLifetime)
        {
            m_data.colors[i] = m_config.colorOverLifetime.evaluate(normalizedAge);
        }

        // Over-lifetime: size (multiplier on start size)
        if (m_config.useSizeOverLifetime)
        {
            float mult = m_config.sizeOverLifetime.evaluate(normalizedAge);
            m_data.sizes[i] = m_data.startSizes[i] * mult;
        }

        // Over-lifetime: speed (multiplier on velocity magnitude)
        if (m_config.useSpeedOverLifetime)
        {
            float speedMult = m_config.speedOverLifetime.evaluate(normalizedAge);
            float currentSpeed = glm::length(m_data.velocities[i]);
            if (currentSpeed > 0.0001f)
            {
                float targetSpeed = m_data.startSpeeds[i] * speedMult;
                m_data.velocities[i] = glm::normalize(m_data.velocities[i]) * targetSpeed;
            }
        }

        // Integrate position
        m_data.positions[i] += m_data.velocities[i] * deltaTime;
    }
}

std::unique_ptr<Component> ParticleEmitterComponent::clone() const
{
    auto copy = std::make_unique<ParticleEmitterComponent>();
    copy->m_config = m_config;
    copy->m_data.resize(m_config.maxParticles);
    copy->m_paused = m_paused;
    return copy;
}

ParticleEmitterConfig& ParticleEmitterComponent::getConfig()
{
    return m_config;
}

const ParticleEmitterConfig& ParticleEmitterComponent::getConfig() const
{
    return m_config;
}

const ParticleData& ParticleEmitterComponent::getData() const
{
    return m_data;
}

void ParticleEmitterComponent::restart()
{
    m_data.clear();
    m_emitAccumulator = 0.0f;
    m_elapsedTime = 0.0f;
}

void ParticleEmitterComponent::setPaused(bool paused)
{
    m_paused = paused;
}

bool ParticleEmitterComponent::isPaused() const
{
    return m_paused;
}

bool ParticleEmitterComponent::isPlaying() const
{
    if (m_paused)
    {
        return false;
    }
    if (m_config.looping)
    {
        return true;
    }
    return m_elapsedTime < m_config.duration;
}

void ParticleEmitterComponent::spawnParticle(const glm::mat4& worldMatrix)
{
    int idx = m_data.count;

    // Position: entity world position + shape offset
    glm::vec3 worldPos = glm::vec3(worldMatrix[3]);
    glm::vec3 offset = generateSpawnOffset();
    // Transform offset by entity rotation/scale (upper-left 3x3)
    glm::mat3 rotation(worldMatrix);
    m_data.positions[idx] = worldPos + rotation * offset;

    // Velocity: shape-based direction * random speed
    float speed = randomFloat(m_config.startSpeedMin, m_config.startSpeedMax);
    glm::vec3 dir = generateSpawnDirection();
    m_data.velocities[idx] = rotation * dir * speed;

    // Color, size, lifetime
    m_data.colors[idx] = m_config.startColor;
    float size = randomFloat(m_config.startSizeMin, m_config.startSizeMax);
    m_data.sizes[idx] = size;
    m_data.startSizes[idx] = size;
    m_data.startSpeeds[idx] = speed;
    m_data.ages[idx] = 0.0f;
    m_data.lifetimes[idx] = randomFloat(m_config.startLifetimeMin, m_config.startLifetimeMax);
    m_data.normalizedAges[idx] = 0.0f;

    ++m_data.count;
}

glm::vec3 ParticleEmitterComponent::generateSpawnOffset() const
{
    switch (m_config.shape)
    {
        case ParticleEmitterConfig::Shape::SPHERE:
        {
            return randomUnitSphere() * randomFloat(0.0f, m_config.shapeRadius);
        }
        case ParticleEmitterConfig::Shape::BOX:
        {
            return glm::vec3(
                randomFloat(-m_config.shapeBoxSize.x * 0.5f, m_config.shapeBoxSize.x * 0.5f),
                randomFloat(-m_config.shapeBoxSize.y * 0.5f, m_config.shapeBoxSize.y * 0.5f),
                randomFloat(-m_config.shapeBoxSize.z * 0.5f, m_config.shapeBoxSize.z * 0.5f));
        }
        case ParticleEmitterConfig::Shape::CONE:
        {
            float r = randomFloat(0.0f, m_config.shapeRadius);
            float angle = randomFloat(0.0f, glm::two_pi<float>());
            return glm::vec3(r * std::cos(angle), 0.0f, r * std::sin(angle));
        }
        case ParticleEmitterConfig::Shape::POINT:
        default:
            return glm::vec3(0.0f);
    }
}

glm::vec3 ParticleEmitterComponent::generateSpawnDirection() const
{
    switch (m_config.shape)
    {
        case ParticleEmitterConfig::Shape::SPHERE:
        {
            return randomUnitSphere();
        }
        case ParticleEmitterConfig::Shape::CONE:
        {
            // Direction within a cone around +Y
            float halfAngle = glm::radians(m_config.shapeConeAngle);
            float cosAngle = std::cos(halfAngle);
            float z = randomFloat(cosAngle, 1.0f);
            float phi = randomFloat(0.0f, glm::two_pi<float>());
            float sinTheta = std::sqrt(1.0f - z * z);
            return glm::normalize(glm::vec3(sinTheta * std::cos(phi), z, sinTheta * std::sin(phi)));
        }
        case ParticleEmitterConfig::Shape::BOX:
        {
            // Upward direction with some random spread
            return glm::normalize(glm::vec3(
                randomFloat(-0.2f, 0.2f),
                1.0f,
                randomFloat(-0.2f, 0.2f)));
        }
        case ParticleEmitterConfig::Shape::POINT:
        default:
        {
            return randomUnitSphere();
        }
    }
}

std::optional<PointLight> ParticleEmitterComponent::getCoupledLight(const glm::vec3& worldPos) const
{
    if (!m_config.emitsLight || m_data.count == 0)
    {
        return std::nullopt;
    }

    // Multi-frequency flicker for organic fire light
    float t = m_elapsedTime * m_config.flickerSpeed;
    float flicker = 0.8f + 0.2f * std::sin(t) * std::sin(t * 1.7f + 0.3f);
    flicker *= (0.9f + 0.1f * std::sin(t * 3.1f + 1.7f));

    PointLight light;
    light.position = worldPos + glm::vec3(0.0f, 0.2f, 0.0f); // Slightly above emitter
    light.diffuse = m_config.lightColor * m_config.lightIntensity * flicker;
    light.ambient = m_config.lightColor * 0.05f;
    light.specular = m_config.lightColor * 0.3f * flicker;

    // Attenuation for the configured range
    light.constant = 1.0f;
    light.linear = 4.5f / m_config.lightRange;
    light.quadratic = 75.0f / (m_config.lightRange * m_config.lightRange);

    light.castsShadow = false; // Flickering shadows would be distracting

    return light;
}

} // namespace Vestige
