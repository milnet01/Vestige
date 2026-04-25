// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_particle_system.cpp
/// @brief GPU compute shader particle pipeline implementation.

#include "renderer/gpu_particle_system.h"
#include "renderer/sampler_fallback.h"
#include "core/logger.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Vestige
{

// Counter buffer layout (matches GLSL)
struct CounterData
{
    uint32_t aliveCount;
    uint32_t deadCount;
    uint32_t emitCount;
    uint32_t maxParticles;
};

// Indirect draw command layout (matches GLSL and OpenGL spec)
struct DrawArraysIndirectCommand
{
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t baseInstance;
};

// GPU particle struct must match GLSL layout (64 bytes, std430)
struct GPUParticleGPU
{
    glm::vec4 position;   // xyz = pos, w = size
    glm::vec4 velocity;   // xyz = vel, w = rotation
    glm::vec4 color;      // rgba
    float age;
    float lifetime;
    float startSize;
    uint32_t flags;
};
static_assert(sizeof(GPUParticleGPU) == 64, "GPUParticle must be 64 bytes for cache alignment");

// Behavior entry layout for UBO (matches GLSL std140)
// std140 requires vec4 alignment for arrays, so we pad accordingly
struct BehaviorEntryGPU
{
    uint32_t type;
    uint32_t flags;
    float params[6];
};
static_assert(sizeof(BehaviorEntryGPU) == 32, "BehaviorEntry must be 32 bytes");

// Full behavior UBO layout (std140)
struct BehaviorBlockGPU
{
    BehaviorEntryGPU behaviors[16];                   // 16 * 32 = 512 bytes
    int32_t behaviorCount;                            // +4
    int32_t colorStopCount;                           // +4
    int32_t sizeKeyCount;                             // +4
    int32_t speedKeyCount;                            // +4  = 528 total
    glm::vec4 colorStops[8];                          // 8 * 16 = 128  = 656
    float colorStopTimes[8];                          // 8 * 4 = 32    = 688
    float sizeKeys[8];                                // 8 * 4 = 32    = 720
    float sizeKeyTimes[8];                            // 8 * 4 = 32    = 752
    float speedKeys[8];                               // 8 * 4 = 32    = 784
    float speedKeyTimes[8];                           // 8 * 4 = 32    = 816
};

GPUParticleSystem::GPUParticleSystem() = default;

GPUParticleSystem::~GPUParticleSystem()
{
    shutdown();
}

bool GPUParticleSystem::init(const std::string& shaderPath, uint32_t maxParticles)
{
    if (m_initialized)
        shutdown();

    m_maxParticles = maxParticles;

    // Load compute shaders
    if (!m_emitShader.loadComputeShader(shaderPath + "/particle_emit.comp.glsl"))
    {
        Logger::error("Failed to load particle emit compute shader");
        return false;
    }
    if (!m_simulateShader.loadComputeShader(shaderPath + "/particle_simulate.comp.glsl"))
    {
        Logger::error("Failed to load particle simulate compute shader");
        return false;
    }
    if (!m_compactShader.loadComputeShader(shaderPath + "/particle_compact.comp.glsl"))
    {
        Logger::error("Failed to load particle compact compute shader");
        return false;
    }
    if (!m_sortShader.loadComputeShader(shaderPath + "/particle_sort.comp.glsl"))
    {
        Logger::error("Failed to load particle sort compute shader");
        return false;
    }
    if (!m_indirectShader.loadComputeShader(shaderPath + "/particle_indirect.comp.glsl"))
    {
        Logger::error("Failed to load particle indirect compute shader");
        return false;
    }

    createBuffers(maxParticles);
    initializeFreeList(maxParticles);

    m_initialized = true;
    Logger::info("GPU particle system initialized (max " + std::to_string(maxParticles) + " particles)");
    return true;
}

void GPUParticleSystem::shutdown()
{
    if (m_particleSSBO) { glDeleteBuffers(1, &m_particleSSBO); m_particleSSBO = 0; }
    if (m_counterSSBO) { glDeleteBuffers(1, &m_counterSSBO); m_counterSSBO = 0; }
    if (m_freeListSSBO) { glDeleteBuffers(1, &m_freeListSSBO); m_freeListSSBO = 0; }
    if (m_indirectDrawSSBO) { glDeleteBuffers(1, &m_indirectDrawSSBO); m_indirectDrawSSBO = 0; }
    if (m_behaviorUBO) { glDeleteBuffers(1, &m_behaviorUBO); m_behaviorUBO = 0; }
    if (m_sortKeySSBO) { glDeleteBuffers(1, &m_sortKeySSBO); m_sortKeySSBO = 0; }
    m_initialized = false;
}

bool GPUParticleSystem::isInitialized() const
{
    return m_initialized;
}

uint32_t GPUParticleSystem::getMaxParticles() const
{
    return m_maxParticles;
}

void GPUParticleSystem::createBuffers(uint32_t maxParticles)
{
    // Particle SSBO (binding 0)
    glCreateBuffers(1, &m_particleSSBO);
    glNamedBufferStorage(m_particleSSBO,
                         maxParticles * sizeof(GPUParticleGPU),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    // Zero-initialize particles (all dead)
    std::vector<GPUParticleGPU> zeros(maxParticles, GPUParticleGPU{});
    glNamedBufferSubData(m_particleSSBO, 0,
                         maxParticles * sizeof(GPUParticleGPU),
                         zeros.data());

    // Counter SSBO (binding 1)
    glCreateBuffers(1, &m_counterSSBO);
    glNamedBufferStorage(m_counterSSBO,
                         sizeof(CounterData),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    // Free list SSBO (binding 2)
    glCreateBuffers(1, &m_freeListSSBO);
    glNamedBufferStorage(m_freeListSSBO,
                         maxParticles * sizeof(uint32_t),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    // Indirect draw command SSBO (binding 3)
    DrawArraysIndirectCommand cmd{6, 0, 0, 0}; // 6 vertices per billboard, 0 instances
    glCreateBuffers(1, &m_indirectDrawSSBO);
    glNamedBufferStorage(m_indirectDrawSSBO,
                         sizeof(DrawArraysIndirectCommand),
                         &cmd,
                         GL_DYNAMIC_STORAGE_BIT);

    // Behavior UBO (binding 4)
    glCreateBuffers(1, &m_behaviorUBO);
    glNamedBufferStorage(m_behaviorUBO,
                         sizeof(BehaviorBlockGPU),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);

    // Sort key SSBO (binding 5) — pairs of (depth, index)
    glCreateBuffers(1, &m_sortKeySSBO);
    glNamedBufferStorage(m_sortKeySSBO,
                         maxParticles * 2 * sizeof(uint32_t),
                         nullptr,
                         GL_DYNAMIC_STORAGE_BIT);
}

void GPUParticleSystem::initializeFreeList(uint32_t maxParticles)
{
    // Fill free list with all indices [0, maxParticles)
    std::vector<uint32_t> indices(maxParticles);
    for (uint32_t i = 0; i < maxParticles; ++i)
        indices[i] = i;

    glNamedBufferSubData(m_freeListSSBO, 0,
                         maxParticles * sizeof(uint32_t),
                         indices.data());

    // Set initial counters: 0 alive, all dead, 0 to emit
    CounterData counters{0, maxParticles, 0, maxParticles};
    glNamedBufferSubData(m_counterSSBO, 0, sizeof(CounterData), &counters);
}

void GPUParticleSystem::beginFrame()
{
    if (!m_initialized)
        return;

    // Reset alive and dead counts — compaction will recount them
    // We do NOT reset here because emit needs deadCount from the previous compact pass.
    // Instead, compact() resets and recounts.
    ++m_frameCount;
}

void GPUParticleSystem::emit(uint32_t count, const EmissionParams& params)
{
    if (!m_initialized || count == 0)
        return;

    // Set emit count in counter buffer
    glNamedBufferSubData(m_counterSSBO,
                         offsetof(CounterData, emitCount),
                         sizeof(uint32_t), &count);

    m_emitShader.use();

    // Bind SSBOs
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_counterSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_freeListSSBO);

    // Set emission uniforms
    m_emitShader.setVec3("u_emitterPos", params.worldPosition);
    m_emitShader.setMat3("u_emitterRotation", params.worldRotation);
    m_emitShader.setInt("u_shapeType", static_cast<int>(params.shapeType));
    m_emitShader.setFloat("u_shapeRadius", params.shapeRadius);
    m_emitShader.setFloat("u_shapeConeAngle", params.shapeConeAngle);
    m_emitShader.setVec3("u_shapeBoxSize", params.shapeBoxSize);
    m_emitShader.setFloat("u_startLifetimeMin", params.startLifetimeMin);
    m_emitShader.setFloat("u_startLifetimeMax", params.startLifetimeMax);
    m_emitShader.setFloat("u_startSpeedMin", params.startSpeedMin);
    m_emitShader.setFloat("u_startSpeedMax", params.startSpeedMax);
    m_emitShader.setFloat("u_startSizeMin", params.startSizeMin);
    m_emitShader.setFloat("u_startSizeMax", params.startSizeMax);
    m_emitShader.setVec4("u_startColor", params.startColor);
    m_emitShader.setInt("u_randomSeed", static_cast<int>(params.randomSeed));

    uint32_t groups = (count + 63) / 64;
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GPUParticleSystem::simulate(float deltaTime, const BehaviorBlock& behaviors, float elapsed)
{
    if (!m_initialized)
        return;

    // Upload behavior data
    uploadBehaviorBlock(behaviors);

    m_simulateShader.use();

    // Bind SSBOs
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_counterSSBO);
    glBindBufferBase(GL_UNIFORM_BUFFER, 4, m_behaviorUBO);

    m_simulateShader.setFloat("u_deltaTime", deltaTime);
    m_simulateShader.setFloat("u_elapsed", elapsed);

    // Depth collision
    m_simulateShader.setBool("u_depthCollision", m_depthCollisionEnabled);
    if (m_depthCollisionEnabled)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_depthTexture);
        m_simulateShader.setInt("u_depthTexture", 0);
        m_simulateShader.setMat4("u_viewProjection", m_depthVP);
        m_simulateShader.setVec2("u_screenSize", m_screenSize);
        m_simulateShader.setFloat("u_cameraNear", m_cameraNear);
    }
    else
    {
        // R6 Mesa fallback: the compute shader's `u_depthTexture`
        // sampler2D is declared regardless of u_depthCollision.
        // Without a sampler2D bound at unit 0 the dispatch fails
        // GL_INVALID_OPERATION on Mesa AMD even when the shader's
        // uniform branch never samples it.
        glBindTextureUnit(0, sharedSamplerFallback().getSampler2D());
        m_simulateShader.setInt("u_depthTexture", 0);
    }

    uint32_t groups = (m_maxParticles + 255) / 256;
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GPUParticleSystem::compact()
{
    if (!m_initialized)
        return;

    // Reset counters before compaction (alive=0, dead=0, emitCount=0 — shader will recount)
    uint32_t zeros[3] = {0, 0, 0};
    glNamedBufferSubData(m_counterSSBO, 0, 3 * sizeof(uint32_t), zeros);

    m_compactShader.use();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_counterSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_freeListSSBO);

    uint32_t groups = (m_maxParticles + 255) / 256;
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GPUParticleSystem::sort(const glm::mat4& viewMatrix)
{
    if (!m_initialized)
        return;

    m_sortShader.use();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_counterSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_sortKeySSBO);

    // Bitonic sort requires power-of-2 array size
    uint32_t n = nextPowerOf2(m_maxParticles);
    uint32_t groups = (n + 255) / 256;

    // Pass 0: Generate sort keys
    m_sortShader.setMat4("u_viewMatrix", viewMatrix);
    m_sortShader.setInt("u_sortPass", 0);
    m_sortShader.setInt("u_sortCount", static_cast<int>(n));

    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Bitonic merge sort passes
    for (uint32_t stage = 1; (1u << stage) <= n; ++stage)
    {
        for (uint32_t step = stage; step >= 1; --step)
        {
            m_sortShader.use();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_sortKeySSBO);

            m_sortShader.setInt("u_sortPass", 1);
            m_sortShader.setInt("u_sortStage", static_cast<int>(stage));
            m_sortShader.setInt("u_sortStep", static_cast<int>(step));
            m_sortShader.setInt("u_sortCount", static_cast<int>(n));

            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }
}

void GPUParticleSystem::updateIndirectCommand()
{
    if (!m_initialized)
        return;

    m_indirectShader.use();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_counterSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_indirectDrawSSBO);

    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

void GPUParticleSystem::setDepthCollision(GLuint depthTexture,
                                          const glm::mat4& viewProjection,
                                          const glm::vec2& screenSize,
                                          float cameraNear)
{
    m_depthCollisionEnabled = true;
    m_depthTexture = depthTexture;
    m_depthVP = viewProjection;
    m_screenSize = screenSize;
    m_cameraNear = cameraNear;
}

void GPUParticleSystem::clearDepthCollision()
{
    m_depthCollisionEnabled = false;
}

void GPUParticleSystem::bindForRendering() const
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_sortKeySSBO);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectDrawSSBO);
}

/*static*/ void GPUParticleSystem::drawIndirect()
{
    glDrawArraysIndirect(GL_TRIANGLES, nullptr);
}

GLuint GPUParticleSystem::getParticleSSBO() const { return m_particleSSBO; }
GLuint GPUParticleSystem::getIndirectBuffer() const { return m_indirectDrawSSBO; }
GLuint GPUParticleSystem::getSortKeySSBO() const { return m_sortKeySSBO; }

uint32_t GPUParticleSystem::readAliveCount() const
{
    if (!m_initialized)
        return 0;
    uint32_t count = 0;
    glGetNamedBufferSubData(m_counterSSBO, offsetof(CounterData, aliveCount),
                            sizeof(uint32_t), &count);
    return count;
}

uint32_t GPUParticleSystem::readDeadCount() const
{
    if (!m_initialized)
        return 0;
    uint32_t count = 0;
    glGetNamedBufferSubData(m_counterSSBO, offsetof(CounterData, deadCount),
                            sizeof(uint32_t), &count);
    return count;
}

void GPUParticleSystem::uploadBehaviorBlock(const BehaviorBlock& behaviors)
{
    BehaviorBlockGPU gpu{};

    for (int i = 0; i < std::min(behaviors.behaviorCount, 16); ++i)
    {
        gpu.behaviors[i].type = static_cast<uint32_t>(behaviors.behaviors[i].type);
        gpu.behaviors[i].flags = behaviors.behaviors[i].flags;
        std::memcpy(gpu.behaviors[i].params, behaviors.behaviors[i].params, sizeof(float) * 6);
    }
    gpu.behaviorCount = behaviors.behaviorCount;

    // Color gradient
    gpu.colorStopCount = behaviors.colorStopCount;
    for (int i = 0; i < std::min(behaviors.colorStopCount, 8); ++i)
    {
        gpu.colorStops[i] = behaviors.colorStops[i];
        gpu.colorStopTimes[i] = behaviors.colorStopTimes[i];
    }

    // Size keys
    gpu.sizeKeyCount = behaviors.sizeKeyCount;
    for (int i = 0; i < std::min(behaviors.sizeKeyCount, 8); ++i)
    {
        gpu.sizeKeys[i] = behaviors.sizeKeys[i];
        gpu.sizeKeyTimes[i] = behaviors.sizeKeyTimes[i];
    }

    // Speed keys
    gpu.speedKeyCount = behaviors.speedKeyCount;
    for (int i = 0; i < std::min(behaviors.speedKeyCount, 8); ++i)
    {
        gpu.speedKeys[i] = behaviors.speedKeys[i];
        gpu.speedKeyTimes[i] = behaviors.speedKeyTimes[i];
    }

    glNamedBufferSubData(m_behaviorUBO, 0, sizeof(BehaviorBlockGPU), &gpu);
}

/*static*/ uint32_t GPUParticleSystem::nextPowerOf2(uint32_t n)
{
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

} // namespace Vestige
