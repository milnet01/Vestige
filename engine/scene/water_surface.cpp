/// @file water_surface.cpp
/// @brief Water surface component implementation — grid mesh generation and management.
#include "scene/water_surface.h"

#include <vector>

namespace Vestige
{

WaterSurfaceComponent::WaterSurfaceComponent() = default;

WaterSurfaceComponent::~WaterSurfaceComponent()
{
    destroyMesh();
}

std::unique_ptr<Component> WaterSurfaceComponent::clone() const
{
    auto copy = std::make_unique<WaterSurfaceComponent>();
    copy->m_config = m_config;
    copy->m_isEnabled = m_isEnabled;
    // Mesh will be built on first rebuildMeshIfNeeded() call
    return copy;
}

WaterSurfaceConfig& WaterSurfaceComponent::getConfig()
{
    return m_config;
}

const WaterSurfaceConfig& WaterSurfaceComponent::getConfig() const
{
    return m_config;
}

void WaterSurfaceComponent::rebuildMeshIfNeeded() const
{
    if (m_vao != 0
        && m_builtWidth == m_config.width
        && m_builtDepth == m_config.depth
        && m_builtResolution == m_config.gridResolution)
    {
        return;  // Mesh is up to date
    }

    buildMesh();
}

GLuint WaterSurfaceComponent::getVao() const
{
    return m_vao;
}

int WaterSurfaceComponent::getIndexCount() const
{
    return m_indexCount;
}

float WaterSurfaceComponent::getLocalWaterY() const
{
    return 0.0f;
}

void WaterSurfaceComponent::buildMesh() const
{
    destroyMesh();

    int res = m_config.gridResolution;
    if (res < 2) res = 2;
    if (res > 256) res = 256;

    float halfW = m_config.width * 0.5f;
    float halfD = m_config.depth * 0.5f;

    // Generate vertices: position (3) + texCoord (2)
    int vertCount = res * res;
    std::vector<float> vertices;
    vertices.reserve(vertCount * 5);

    for (int z = 0; z < res; ++z)
    {
        for (int x = 0; x < res; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(res - 1);
            float v = static_cast<float>(z) / static_cast<float>(res - 1);

            float px = -halfW + u * m_config.width;
            float pz = -halfD + v * m_config.depth;

            vertices.push_back(px);
            vertices.push_back(0.0f);  // Y = 0 in local space
            vertices.push_back(pz);
            vertices.push_back(u);
            vertices.push_back(v);
        }
    }

    // Generate indices (two triangles per quad)
    int quadsPerRow = res - 1;
    m_indexCount = quadsPerRow * quadsPerRow * 6;
    std::vector<unsigned int> indices;
    indices.reserve(m_indexCount);

    for (int z = 0; z < quadsPerRow; ++z)
    {
        for (int x = 0; x < quadsPerRow; ++x)
        {
            unsigned int topLeft = z * res + x;
            unsigned int topRight = topLeft + 1;
            unsigned int bottomLeft = (z + 1) * res + x;
            unsigned int bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    // Upload to GPU
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(), GL_STATIC_DRAW);

    // Position: location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(0));

    // TexCoord: location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));

    glBindVertexArray(0);

    m_builtWidth = m_config.width;
    m_builtDepth = m_config.depth;
    m_builtResolution = res;
}

void WaterSurfaceComponent::destroyMesh() const
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo != 0)
    {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    m_indexCount = 0;
    m_builtWidth = 0.0f;
    m_builtDepth = 0.0f;
    m_builtResolution = 0;
}

} // namespace Vestige
