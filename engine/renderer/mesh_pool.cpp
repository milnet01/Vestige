// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file mesh_pool.cpp
/// @brief Shared mega-buffer implementation for MDI rendering.
#include "renderer/mesh_pool.h"
#include "core/logger.h"

namespace Vestige
{

MeshPool::MeshPool()
{
    glCreateVertexArrays(1, &m_vao);
}

MeshPool::~MeshPool()
{
    if (m_vao != 0) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo != 0) glDeleteBuffers(1, &m_vbo);
    if (m_ibo != 0) glDeleteBuffers(1, &m_ibo);
}

MeshPoolEntry MeshPool::registerMesh(const Mesh* mesh,
                                      const std::vector<Vertex>& vertices,
                                      const std::vector<uint32_t>& indices)
{
    // Check if already registered
    auto it = m_entries.find(mesh);
    if (it != m_entries.end())
    {
        return it->second;
    }

    MeshPoolEntry entry;
    entry.baseVertex = static_cast<int32_t>(m_allVertices.size());
    entry.firstIndex = static_cast<uint32_t>(m_allIndices.size());
    entry.indexCount = static_cast<uint32_t>(indices.size());

    // Append data to the mega-buffers
    m_allVertices.insert(m_allVertices.end(), vertices.begin(), vertices.end());
    m_allIndices.insert(m_allIndices.end(), indices.begin(), indices.end());

    m_entries[mesh] = entry;
    m_dirty = true;

    return entry;
}

bool MeshPool::hasMesh(const Mesh* mesh) const
{
    return m_entries.find(mesh) != m_entries.end();
}

MeshPoolEntry MeshPool::getEntry(const Mesh* mesh) const
{
    auto it = m_entries.find(mesh);
    if (it != m_entries.end())
    {
        return it->second;
    }
    return MeshPoolEntry{};
}

void MeshPool::bind() const
{
    glBindVertexArray(m_vao);
}

void MeshPool::unbind() const
{
    glBindVertexArray(0);
}

GLuint MeshPool::getVao() const
{
    return m_vao;
}

bool MeshPool::hasData() const
{
    return !m_entries.empty() && !m_dirty;
}

void MeshPool::rebuild()
{
    if (m_allVertices.empty() || m_allIndices.empty())
    {
        m_dirty = false;
        return;
    }

    // Delete old buffers if they exist (immutable storage can't be resized)
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ibo != 0)
    {
        glDeleteBuffers(1, &m_ibo);
        m_ibo = 0;
    }

    // Create new VBO with immutable storage (static data, never changes)
    glCreateBuffers(1, &m_vbo);
    auto vboSize = static_cast<GLsizeiptr>(m_allVertices.size() * sizeof(Vertex));
    glNamedBufferStorage(m_vbo, vboSize, m_allVertices.data(), 0);

    // Create new IBO with immutable storage
    glCreateBuffers(1, &m_ibo);
    auto iboSize = static_cast<GLsizeiptr>(m_allIndices.size() * sizeof(uint32_t));
    glNamedBufferStorage(m_ibo, iboSize, m_allIndices.data(), 0);

    // Set up the shared VAO
    setupVao();

    m_dirty = false;

    Logger::info("MeshPool rebuilt: "
        + std::to_string(m_allVertices.size()) + " vertices, "
        + std::to_string(m_allIndices.size()) + " indices, "
        + std::to_string(m_entries.size()) + " meshes");
}

void MeshPool::setupVao()
{
    // Bind the shared VBO to binding point 0
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0,
                              static_cast<GLsizei>(sizeof(Vertex)));

    // Bind the shared IBO
    glVertexArrayElementBuffer(m_vao, m_ibo);

    // Position (location 0)
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE,
                              offsetof(Vertex, position));
    glVertexArrayAttribBinding(m_vao, 0, 0);

    // Normal (location 1)
    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE,
                              offsetof(Vertex, normal));
    glVertexArrayAttribBinding(m_vao, 1, 0);

    // Color (location 2)
    glEnableVertexArrayAttrib(m_vao, 2);
    glVertexArrayAttribFormat(m_vao, 2, 3, GL_FLOAT, GL_FALSE,
                              offsetof(Vertex, color));
    glVertexArrayAttribBinding(m_vao, 2, 0);

    // TexCoord (location 3)
    glEnableVertexArrayAttrib(m_vao, 3);
    glVertexArrayAttribFormat(m_vao, 3, 2, GL_FLOAT, GL_FALSE,
                              offsetof(Vertex, texCoord));
    glVertexArrayAttribBinding(m_vao, 3, 0);

    // Tangent (location 4)
    glEnableVertexArrayAttrib(m_vao, 4);
    glVertexArrayAttribFormat(m_vao, 4, 3, GL_FLOAT, GL_FALSE,
                              offsetof(Vertex, tangent));
    glVertexArrayAttribBinding(m_vao, 4, 0);

    // Bitangent (location 5)
    glEnableVertexArrayAttrib(m_vao, 5);
    glVertexArrayAttribFormat(m_vao, 5, 3, GL_FLOAT, GL_FALSE,
                              offsetof(Vertex, bitangent));
    glVertexArrayAttribBinding(m_vao, 5, 0);
}

} // namespace Vestige
