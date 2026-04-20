// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_component.cpp
/// @brief ClothComponent implementation — thin wrapper around an
///        `IClothSolverBackend` (CPU XPBD or GPU compute).

#include "physics/cloth_component.h"
#include "physics/cloth_simulator.h"  // For ClothConfig definition.
#include "profiler/cpu_profiler.h"

namespace Vestige
{

void ClothComponent::initialize(const ClothConfig& config, std::shared_ptr<Material> material,
                                uint32_t seed)
{
    m_material = std::move(material);

    // Backend selection — factory consults particle-count threshold +
    // GpuClothSimulator::isSupported() to pick CPU vs GPU. Callers override
    // via setBackendPolicy() before initialize().
    m_simulator = createClothSolverBackend(config, m_backendPolicy, m_shaderPath);
    m_simulator->initialize(config, seed);

    uint32_t count = m_simulator->getParticleCount();
    const glm::vec3* positions = m_simulator->getPositions();
    const glm::vec3* normals   = m_simulator->getNormals();
    const auto& texCoords      = m_simulator->getTexCoords();
    const auto& indices        = m_simulator->getIndices();

    // Build initial vertex buffer.
    m_vertexBuffer.resize(count);
    glm::vec3 white(1.0f);
    for (uint32_t i = 0; i < count; ++i)
    {
        m_vertexBuffer[i].position = positions[i];
        m_vertexBuffer[i].normal = normals[i];
        m_vertexBuffer[i].color = white;
        m_vertexBuffer[i].texCoord = texCoords[i];
        m_vertexBuffer[i].tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        m_vertexBuffer[i].bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    std::vector<uint32_t> indicesCopy(indices.begin(), indices.end());
    calculateTangents(m_vertexBuffer, indicesCopy);
    m_mesh.create(m_vertexBuffer, indicesCopy);

    m_ready = true;
}

void ClothComponent::update(float deltaTime)
{
    VESTIGE_PROFILE_SCOPE("ClothSim");
    if (!m_ready || !m_isEnabled || !m_simulator)
    {
        return;
    }

    m_timeAccumulator += deltaTime;

    int steps = 0;
    while (m_timeAccumulator >= FIXED_DT && steps < MAX_STEPS_PER_FRAME)
    {
        m_simulator->simulate(FIXED_DT);
        m_timeAccumulator -= FIXED_DT;
        ++steps;
    }

    if (m_timeAccumulator > FIXED_DT * static_cast<float>(MAX_STEPS_PER_FRAME))
    {
        m_timeAccumulator = 0.0f;
    }

    if (steps == 0) return;

    uint32_t count = m_simulator->getParticleCount();
    const glm::vec3* positions = m_simulator->getPositions();
    const glm::vec3* normals   = m_simulator->getNormals();

    for (uint32_t i = 0; i < count; ++i)
    {
        m_vertexBuffer[i].position = positions[i];
        m_vertexBuffer[i].normal = normals[i];
    }

    if (m_material && m_material->hasNormalMap())
    {
        calculateTangents(m_vertexBuffer, m_simulator->getIndices());
    }

    m_mesh.updateVertices(m_vertexBuffer);
}

void ClothComponent::syncMesh()
{
    if (!m_ready || !m_simulator) return;

    m_simulator->simulate(0.0001f);

    uint32_t count = m_simulator->getParticleCount();
    const glm::vec3* positions = m_simulator->getPositions();
    const glm::vec3* normals   = m_simulator->getNormals();

    for (uint32_t i = 0; i < count; ++i)
    {
        m_vertexBuffer[i].position = positions[i];
        m_vertexBuffer[i].normal = normals[i];
    }

    calculateTangents(m_vertexBuffer, m_simulator->getIndices());
    m_mesh.updateVertices(m_vertexBuffer);
}

void ClothComponent::reset()
{
    if (!m_ready || !m_simulator) return;
    m_simulator->reset();
    syncMesh();
}

void ClothComponent::applyPreset(ClothPresetType type)
{
    if (!m_ready || !m_simulator) return;

    if (type == ClothPresetType::CUSTOM)
    {
        m_presetType = ClothPresetType::CUSTOM;
        return;
    }

    ClothPresetConfig preset = ClothPresets::getPresetConfig(type);

    m_simulator->setParticleMass(preset.solver.particleMass);
    m_simulator->setSubsteps(preset.solver.substeps);
    m_simulator->setDamping(preset.solver.damping);
    m_simulator->setStretchCompliance(preset.solver.stretchCompliance);
    m_simulator->setShearCompliance(preset.solver.shearCompliance);
    m_simulator->setBendCompliance(preset.solver.bendCompliance);

    glm::vec3 windVel = m_simulator->getWindVelocity();
    float windLen = glm::length(windVel);
    glm::vec3 windDir = (windLen > 0.001f) ? windVel / windLen : glm::vec3(0.0f, 0.0f, 1.0f);
    m_simulator->setWind(windDir, preset.windStrength);
    m_simulator->setDragCoefficient(preset.dragCoefficient);

    m_presetType = type;
}

IClothSolverBackend& ClothComponent::getSimulator()
{
    return *m_simulator;
}

const IClothSolverBackend& ClothComponent::getSimulator() const
{
    return *m_simulator;
}

DynamicMesh& ClothComponent::getMesh()             { return m_mesh; }
const DynamicMesh& ClothComponent::getMesh() const { return m_mesh; }
const std::shared_ptr<Material>& ClothComponent::getMaterial() const { return m_material; }
bool ClothComponent::isReady() const { return m_ready; }

} // namespace Vestige
