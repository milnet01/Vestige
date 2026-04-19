// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_particle_emitter.cpp
/// @brief GPU-accelerated particle emitter implementation.

#include "scene/gpu_particle_emitter.h"
#include "core/logger.h"

#include <algorithm>
#include <cstring>

namespace Vestige
{

GPUParticleEmitter::GPUParticleEmitter()
    : m_gpuSystem(std::make_unique<GPUParticleSystem>())
{
}

GPUParticleEmitter::~GPUParticleEmitter() = default;

bool GPUParticleEmitter::init(const std::string& shaderPath)
{
    m_shaderPath = shaderPath;

    uint32_t maxParticles = static_cast<uint32_t>(m_config.maxParticles);
    if (maxParticles == 0)
        maxParticles = 1000;

    m_gpuInitialized = m_gpuSystem->init(shaderPath, maxParticles);
    if (!m_gpuInitialized)
    {
        Logger::warning("GPU particle system init failed — GPU path unavailable");
    }
    return m_gpuInitialized;
}

void GPUParticleEmitter::update(float deltaTime)
{
    if (!isEnabled() || m_paused || !m_gpuInitialized)
        return;

    m_elapsedTime += deltaTime;

    // Check if non-looping emitter has expired
    if (!m_config.looping && m_elapsedTime > m_config.duration)
        return;

    // Rebuild behavior block if dirty
    if (m_behaviorsDirty)
    {
        buildBehaviorBlock();
        m_behaviorsDirty = false;
    }

    // Calculate particles to spawn this frame
    m_emitAccumulator += m_config.emissionRate * deltaTime;
    uint32_t toEmit = static_cast<uint32_t>(m_emitAccumulator);
    m_emitAccumulator -= static_cast<float>(toEmit);

    // GPU pipeline: begin → emit → simulate → compact → indirect update
    m_gpuSystem->beginFrame();

    if (toEmit > 0)
    {
        EmissionParams params = buildEmissionParams();
        m_gpuSystem->emit(toEmit, params);
    }

    m_gpuSystem->simulate(deltaTime, m_behaviorBlock, m_elapsedTime);
    m_gpuSystem->compact();
    m_gpuSystem->updateIndirectCommand();
}

std::unique_ptr<Component> GPUParticleEmitter::clone() const
{
    auto copy = std::make_unique<GPUParticleEmitter>();
    copy->m_config = m_config;
    copy->m_behaviors = m_behaviors;
    copy->m_behaviorsDirty = true;
    copy->m_shaderPath = m_shaderPath;
    // GPU system will be initialized separately
    return copy;
}

void GPUParticleEmitter::setConfig(const ParticleEmitterConfig& config)
{
    bool needsReinit = (config.maxParticles != m_config.maxParticles);
    m_config = config;

    if (needsReinit && m_gpuInitialized && !m_shaderPath.empty())
    {
        m_gpuSystem->shutdown();
        m_gpuInitialized = m_gpuSystem->init(m_shaderPath,
                                             static_cast<uint32_t>(m_config.maxParticles));
    }

    // Rebuild behaviors from config
    buildBehaviorsFromConfig();
}

ParticleEmitterConfig& GPUParticleEmitter::getConfig()
{
    return m_config;
}

const ParticleEmitterConfig& GPUParticleEmitter::getConfig() const
{
    return m_config;
}

void GPUParticleEmitter::addBehavior(ParticleBehaviorType type, const BehaviorParams& params)
{
    if (m_behaviors.size() >= 16)
    {
        Logger::warning("Maximum 16 behaviors per GPU particle emitter");
        return;
    }

    m_behaviors.push_back({type, params});
    m_behaviorsDirty = true;
}

void GPUParticleEmitter::removeBehavior(ParticleBehaviorType type)
{
    m_behaviors.erase(
        std::remove_if(m_behaviors.begin(), m_behaviors.end(),
                       [type](const BehaviorSlot& b) { return b.type == type; }),
        m_behaviors.end());
    m_behaviorsDirty = true;
}

void GPUParticleEmitter::clearBehaviors()
{
    m_behaviors.clear();
    m_behaviorsDirty = true;
}

int GPUParticleEmitter::getBehaviorCount() const
{
    return static_cast<int>(m_behaviors.size());
}

void GPUParticleEmitter::setPaused(bool paused)
{
    m_paused = paused;
}

bool GPUParticleEmitter::isPaused() const
{
    return m_paused;
}

void GPUParticleEmitter::restart()
{
    m_elapsedTime = 0.0f;
    m_emitAccumulator = 0.0f;

    if (m_gpuInitialized && !m_shaderPath.empty())
    {
        m_gpuSystem->shutdown();
        m_gpuInitialized = m_gpuSystem->init(m_shaderPath,
                                             static_cast<uint32_t>(m_config.maxParticles));
    }
}

bool GPUParticleEmitter::isPlaying() const
{
    if (m_paused) return false;
    if (!m_config.looping && m_elapsedTime > m_config.duration) return false;
    return true;
}

uint32_t GPUParticleEmitter::getAliveCount() const
{
    if (m_gpuInitialized)
        m_cachedAliveCount = m_gpuSystem->readAliveCount();
    return m_cachedAliveCount;
}

bool GPUParticleEmitter::isGPUPath() const
{
    return m_gpuInitialized;
}

GPUParticleSystem* GPUParticleEmitter::getGPUSystem()
{
    return m_gpuSystem.get();
}

const GPUParticleSystem* GPUParticleEmitter::getGPUSystem() const
{
    return m_gpuSystem.get();
}

ParticleEmitterConfig::BlendMode GPUParticleEmitter::getBlendMode() const
{
    return m_config.blendMode;
}

const std::string& GPUParticleEmitter::getTexturePath() const
{
    return m_config.texturePath;
}

bool GPUParticleEmitter::needsSorting() const
{
    return m_config.blendMode == ParticleEmitterConfig::BlendMode::ALPHA_BLEND;
}

void GPUParticleEmitter::enableDepthCollision(GLuint depthTexture,
                                              const glm::mat4& viewProjection,
                                              const glm::vec2& screenSize,
                                              float cameraNear)
{
    if (m_gpuInitialized)
        m_gpuSystem->setDepthCollision(depthTexture, viewProjection, screenSize, cameraNear);
}

void GPUParticleEmitter::disableDepthCollision()
{
    if (m_gpuInitialized)
        m_gpuSystem->clearDepthCollision();
}

void GPUParticleEmitter::buildBehaviorsFromConfig()
{
    clearBehaviors();

    // Add gravity behavior from config
    if (glm::length(m_config.gravity) > 0.001f)
    {
        BehaviorParams gp;
        gp.values[0] = m_config.gravity.x;
        gp.values[1] = m_config.gravity.y;
        gp.values[2] = m_config.gravity.z;
        addBehavior(ParticleBehaviorType::GRAVITY, gp);
    }

    // Convert over-lifetime color gradient to behavior block
    if (m_config.useColorOverLifetime)
    {
        const auto& stops = m_config.colorOverLifetime.stops;
        int stopCount = std::min(static_cast<int>(stops.size()), 8);
        m_behaviorBlock.colorStopCount = stopCount;
        for (int i = 0; i < stopCount; ++i)
        {
            const size_t u = static_cast<size_t>(i);
            m_behaviorBlock.colorStops[i] = stops[u].color;
            m_behaviorBlock.colorStopTimes[i] = stops[u].position;
        }
    }
    else
    {
        m_behaviorBlock.colorStopCount = 0;
    }

    // Convert over-lifetime size curve
    if (m_config.useSizeOverLifetime)
    {
        const auto& keys = m_config.sizeOverLifetime.keyframes;
        int keyCount = std::min(static_cast<int>(keys.size()), 8);
        m_behaviorBlock.sizeKeyCount = keyCount;
        for (int i = 0; i < keyCount; ++i)
        {
            const size_t u = static_cast<size_t>(i);
            m_behaviorBlock.sizeKeys[i] = keys[u].value;
            m_behaviorBlock.sizeKeyTimes[i] = keys[u].time;
        }
    }
    else
    {
        m_behaviorBlock.sizeKeyCount = 0;
    }

    // Convert over-lifetime speed curve
    if (m_config.useSpeedOverLifetime)
    {
        const auto& keys = m_config.speedOverLifetime.keyframes;
        int keyCount = std::min(static_cast<int>(keys.size()), 8);
        m_behaviorBlock.speedKeyCount = keyCount;
        for (int i = 0; i < keyCount; ++i)
        {
            const size_t u = static_cast<size_t>(i);
            m_behaviorBlock.speedKeys[i] = keys[u].value;
            m_behaviorBlock.speedKeyTimes[i] = keys[u].time;
        }
    }
    else
    {
        m_behaviorBlock.speedKeyCount = 0;
    }

    m_behaviorsDirty = true;
}

void GPUParticleEmitter::buildBehaviorBlock()
{
    // Copy behavior entries into the block
    m_behaviorBlock.behaviorCount = static_cast<int>(m_behaviors.size());
    for (size_t i = 0; i < m_behaviors.size() && i < 16; ++i)
    {
        m_behaviorBlock.behaviors[i].type = m_behaviors[i].type;
        m_behaviorBlock.behaviors[i].flags = 0;
        std::memcpy(m_behaviorBlock.behaviors[i].params,
                    m_behaviors[i].params.values, sizeof(float) * 6);
    }
}

EmissionParams GPUParticleEmitter::buildEmissionParams() const
{
    EmissionParams params;
    params.worldPosition = glm::vec3(0.0f); // Will be set from entity world matrix
    params.worldRotation = glm::mat3(1.0f);

    // Map ParticleEmitterConfig shape to GPU shape type
    switch (m_config.shape)
    {
        case ParticleEmitterConfig::Shape::POINT:
            params.shapeType = 0;
            break;
        case ParticleEmitterConfig::Shape::SPHERE:
            params.shapeType = 1;
            params.shapeRadius = m_config.shapeRadius;
            break;
        case ParticleEmitterConfig::Shape::CONE:
            params.shapeType = 2;
            params.shapeRadius = m_config.shapeRadius;
            params.shapeConeAngle = glm::radians(m_config.shapeConeAngle);
            break;
        case ParticleEmitterConfig::Shape::BOX:
            params.shapeType = 3;
            params.shapeBoxSize = m_config.shapeBoxSize;
            break;
    }

    params.startLifetimeMin = m_config.startLifetimeMin;
    params.startLifetimeMax = m_config.startLifetimeMax;
    params.startSpeedMin = m_config.startSpeedMin;
    params.startSpeedMax = m_config.startSpeedMax;
    params.startSizeMin = m_config.startSizeMin;
    params.startSizeMax = m_config.startSizeMax;
    params.startColor = m_config.startColor;

    // Generate unique random seed per emission frame
    params.randomSeed = static_cast<uint32_t>(m_elapsedTime * 100000.0f) ^ 0xDEADBEEF;

    return params;
}

} // namespace Vestige
