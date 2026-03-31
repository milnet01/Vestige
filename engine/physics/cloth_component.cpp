/// @file cloth_component.cpp
/// @brief ClothComponent implementation.
#include "physics/cloth_component.h"

namespace Vestige
{

void ClothComponent::initialize(const ClothConfig& config, std::shared_ptr<Material> material,
                                uint32_t seed)
{
    m_material = std::move(material);

    // Initialize the simulator (generates particle grid + constraints)
    m_simulator.initialize(config, seed);

    uint32_t count = m_simulator.getParticleCount();
    const glm::vec3* positions = m_simulator.getPositions();
    const glm::vec3* normals = m_simulator.getNormals();
    const auto& texCoords = m_simulator.getTexCoords();
    const auto& indices = m_simulator.getIndices();

    // Build initial vertex buffer
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

    // Compute tangents from the initial mesh
    std::vector<uint32_t> indicesCopy(indices.begin(), indices.end());
    calculateTangents(m_vertexBuffer, indicesCopy);

    // Create the GPU mesh
    m_mesh.create(m_vertexBuffer, indicesCopy);

    m_ready = true;
}

void ClothComponent::update(float deltaTime)
{
    if (!m_ready || !m_isEnabled)
    {
        return;
    }

    // Run physics simulation
    m_simulator.simulate(deltaTime);

    // Copy updated positions and normals into vertex buffer
    uint32_t count = m_simulator.getParticleCount();
    const glm::vec3* positions = m_simulator.getPositions();
    const glm::vec3* normals = m_simulator.getNormals();

    for (uint32_t i = 0; i < count; ++i)
    {
        m_vertexBuffer[i].position = positions[i];
        m_vertexBuffer[i].normal = normals[i];
    }

    // Recompute tangents for correct normal mapping on deformed geometry
    std::vector<uint32_t> indices(m_simulator.getIndices().begin(),
                                   m_simulator.getIndices().end());
    calculateTangents(m_vertexBuffer, indices);

    // Upload to GPU
    m_mesh.updateVertices(m_vertexBuffer);
}

void ClothComponent::syncMesh()
{
    if (!m_ready)
    {
        return;
    }

    // Trigger normal recomputation via a tiny simulation step
    m_simulator.simulate(0.0001f);

    uint32_t count = m_simulator.getParticleCount();
    const glm::vec3* positions = m_simulator.getPositions();
    const glm::vec3* normals = m_simulator.getNormals();

    for (uint32_t i = 0; i < count; ++i)
    {
        m_vertexBuffer[i].position = positions[i];
        m_vertexBuffer[i].normal = normals[i];
    }

    std::vector<uint32_t> indices(m_simulator.getIndices().begin(),
                                   m_simulator.getIndices().end());
    calculateTangents(m_vertexBuffer, indices);
    m_mesh.updateVertices(m_vertexBuffer);
}

ClothSimulator& ClothComponent::getSimulator()
{
    return m_simulator;
}

const ClothSimulator& ClothComponent::getSimulator() const
{
    return m_simulator;
}

DynamicMesh& ClothComponent::getMesh()
{
    return m_mesh;
}

const DynamicMesh& ClothComponent::getMesh() const
{
    return m_mesh;
}

const std::shared_ptr<Material>& ClothComponent::getMaterial() const
{
    return m_material;
}

bool ClothComponent::isReady() const
{
    return m_ready;
}

} // namespace Vestige
