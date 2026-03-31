/// @file dynamic_mesh.h
/// @brief GPU mesh with per-frame vertex streaming for deformable geometry (cloth, soft bodies).
#pragma once

#include "renderer/mesh.h"
#include "utils/aabb.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief A mesh whose vertex data can be updated every frame via glNamedBufferSubData.
/// Uses the same Vertex format and VAO layout as Mesh, but allocates with
/// GL_DYNAMIC_STORAGE_BIT so the VBO is mutable.
class DynamicMesh
{
public:
    DynamicMesh();
    ~DynamicMesh();

    // Non-copyable (owns GPU resources)
    DynamicMesh(const DynamicMesh&) = delete;
    DynamicMesh& operator=(const DynamicMesh&) = delete;

    // Movable
    DynamicMesh(DynamicMesh&& other) noexcept;
    DynamicMesh& operator=(DynamicMesh&& other) noexcept;

    /// @brief Creates the GPU buffers and uploads initial vertex/index data.
    /// Call once during setup. The vertex buffer is allocated with
    /// GL_DYNAMIC_STORAGE_BIT so it can be updated later.
    void create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    /// @brief Replaces the vertex data on the GPU. The vertex count must match
    /// the count passed to create(). Index data is unchanged.
    void updateVertices(const std::vector<Vertex>& vertices);

    /// @brief Binds the vertex array for rendering.
    void bind() const;

    /// @brief Unbinds the vertex array.
    void unbind() const;

    /// @brief Gets the number of indices (for glDrawElements).
    uint32_t getIndexCount() const;

    /// @brief Gets the VAO handle.
    GLuint getVao() const;

    /// @brief Gets the local-space bounding box (recomputed on updateVertices).
    const AABB& getLocalBounds() const;

    /// @brief Returns true if create() has been called successfully.
    bool isCreated() const;

private:
    void cleanup();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    uint32_t m_indexCount = 0;
    uint32_t m_vertexCount = 0;
    AABB m_localBounds;
};

} // namespace Vestige
