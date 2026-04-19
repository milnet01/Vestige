// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file dynamic_mesh.cpp
/// @brief DynamicMesh implementation.
#include "renderer/dynamic_mesh.h"

#include <limits>

namespace Vestige
{

DynamicMesh::DynamicMesh() = default;

DynamicMesh::~DynamicMesh()
{
    cleanup();
}

DynamicMesh::DynamicMesh(DynamicMesh&& other) noexcept
    : m_vao(other.m_vao)
    , m_vbo(other.m_vbo)
    , m_ebo(other.m_ebo)
    , m_indexCount(other.m_indexCount)
    , m_vertexCount(other.m_vertexCount)
    , m_localBounds(other.m_localBounds)
{
    other.m_vao = 0;
    other.m_vbo = 0;
    other.m_ebo = 0;
    other.m_indexCount = 0;
    other.m_vertexCount = 0;
    other.m_localBounds = {};
}

DynamicMesh& DynamicMesh::operator=(DynamicMesh&& other) noexcept
{
    if (this != &other)
    {
        cleanup();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_indexCount = other.m_indexCount;
        m_vertexCount = other.m_vertexCount;
        m_localBounds = other.m_localBounds;
        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_ebo = 0;
        other.m_indexCount = 0;
        other.m_vertexCount = 0;
        other.m_localBounds = {};
    }
    return *this;
}

void DynamicMesh::create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    cleanup();

    m_vertexCount = static_cast<uint32_t>(vertices.size());
    m_indexCount = static_cast<uint32_t>(indices.size());

    // VBO with GL_DYNAMIC_STORAGE_BIT — allows glNamedBufferSubData updates
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(), GL_DYNAMIC_STORAGE_BIT);

    // EBO is immutable — topology doesn't change
    glCreateBuffers(1, &m_ebo);
    glNamedBufferStorage(m_ebo,
        static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
        indices.data(), 0);

    // Create VAO with DSA (identical layout to Mesh)
    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(m_vao, m_ebo);

    // Position (location 0)
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(m_vao, 0, 0);

    // Normal (location 1)
    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(m_vao, 1, 0);

    // Color (location 2)
    glEnableVertexArrayAttrib(m_vao, 2);
    glVertexArrayAttribFormat(m_vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
    glVertexArrayAttribBinding(m_vao, 2, 0);

    // TexCoord (location 3)
    glEnableVertexArrayAttrib(m_vao, 3);
    glVertexArrayAttribFormat(m_vao, 3, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, texCoord));
    glVertexArrayAttribBinding(m_vao, 3, 0);

    // Tangent (location 4)
    glEnableVertexArrayAttrib(m_vao, 4);
    glVertexArrayAttribFormat(m_vao, 4, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, tangent));
    glVertexArrayAttribBinding(m_vao, 4, 0);

    // Bitangent (location 5)
    glEnableVertexArrayAttrib(m_vao, 5);
    glVertexArrayAttribFormat(m_vao, 5, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, bitangent));
    glVertexArrayAttribBinding(m_vao, 5, 0);

    // Bone IDs (location 10)
    glEnableVertexArrayAttrib(m_vao, 10);
    glVertexArrayAttribIFormat(m_vao, 10, 4, GL_INT, offsetof(Vertex, boneIds));
    glVertexArrayAttribBinding(m_vao, 10, 0);

    // Bone Weights (location 11)
    glEnableVertexArrayAttrib(m_vao, 11);
    glVertexArrayAttribFormat(m_vao, 11, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, boneWeights));
    glVertexArrayAttribBinding(m_vao, 11, 0);

    // Compute initial AABB
    if (!vertices.empty())
    {
        glm::vec3 bmin(std::numeric_limits<float>::max());
        glm::vec3 bmax(std::numeric_limits<float>::lowest());
        for (const auto& v : vertices)
        {
            bmin = glm::min(bmin, v.position);
            bmax = glm::max(bmax, v.position);
        }
        m_localBounds = {bmin, bmax};
    }
}

void DynamicMesh::updateVertices(const std::vector<Vertex>& vertices)
{
    if (m_vbo == 0 || vertices.size() != m_vertexCount)
    {
        return;
    }

    glNamedBufferSubData(m_vbo, 0,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data());

    // Recompute AABB
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    for (const auto& v : vertices)
    {
        bmin = glm::min(bmin, v.position);
        bmax = glm::max(bmax, v.position);
    }
    m_localBounds = {bmin, bmax};
}

void DynamicMesh::bind() const
{
    glBindVertexArray(m_vao);
}

/*static*/ void DynamicMesh::unbind()
{
    glBindVertexArray(0);
}

uint32_t DynamicMesh::getIndexCount() const
{
    return m_indexCount;
}

GLuint DynamicMesh::getVao() const
{
    return m_vao;
}

const AABB& DynamicMesh::getLocalBounds() const
{
    return m_localBounds;
}

bool DynamicMesh::isCreated() const
{
    return m_vao != 0;
}

void DynamicMesh::cleanup()
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
    m_vertexCount = 0;
}

} // namespace Vestige
