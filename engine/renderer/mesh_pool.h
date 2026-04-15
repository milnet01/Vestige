// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file mesh_pool.h
/// @brief Shared mega-buffer for packing all scene mesh geometry into one VBO/IBO.
#pragma once

#include "renderer/mesh.h"

#include <glad/gl.h>

#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Entry in the mesh pool tracking where a mesh's data is stored.
struct MeshPoolEntry
{
    int32_t baseVertex = 0;     // Offset into the shared VBO (in vertices)
    uint32_t firstIndex = 0;    // Offset into the shared IBO (in indices)
    uint32_t indexCount = 0;    // Number of indices for this mesh
};

/// @brief Packs all scene meshes into shared VBO/IBO with a single VAO.
///
/// This enables Multi-Draw Indirect (MDI) by allowing different meshes
/// to be drawn with a single VAO binding. Each mesh is identified by
/// its (baseVertex, firstIndex, indexCount) in the shared buffers.
class MeshPool
{
public:
    MeshPool();
    ~MeshPool();

    // Non-copyable
    MeshPool(const MeshPool&) = delete;
    MeshPool& operator=(const MeshPool&) = delete;

    /// @brief Registers a mesh in the pool. Returns its pool entry.
    /// If already registered, returns the existing entry without re-uploading.
    /// @param mesh The mesh to register (must have been uploaded already).
    /// @param vertices The mesh's vertex data.
    /// @param indices The mesh's index data.
    MeshPoolEntry registerMesh(const Mesh* mesh,
                                const std::vector<Vertex>& vertices,
                                const std::vector<uint32_t>& indices);

    /// @brief Checks if a mesh is already registered.
    bool hasMesh(const Mesh* mesh) const;

    /// @brief Gets the pool entry for a registered mesh.
    MeshPoolEntry getEntry(const Mesh* mesh) const;

    /// @brief Binds the shared VAO for rendering.
    void bind() const;

    /// @brief Unbinds the shared VAO.
    void unbind() const;

    /// @brief Gets the shared VAO handle (for instance attribute setup).
    GLuint getVao() const;

    /// @brief Returns true if any meshes have been registered.
    bool hasData() const;

    /// @brief Rebuilds the shared VBO/IBO from all registered vertex/index data.
    /// Call this after all meshes have been registered.
    void rebuild();

private:
    void setupVao();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ibo = 0;

    // All vertex/index data packed contiguously
    std::vector<Vertex> m_allVertices;
    std::vector<uint32_t> m_allIndices;

    // Map from mesh pointer to pool entry
    std::unordered_map<const Mesh*, MeshPoolEntry> m_entries;

    bool m_dirty = false;  // True if data has been added but not rebuilt
};

} // namespace Vestige
