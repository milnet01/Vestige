/// @file cloth_mesh_collider.h
/// @brief Triangle mesh collider for cloth simulation using BVH acceleration.
#pragma once

#include "physics/bvh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief Triangle mesh collider for cloth-body collision.
///
/// Owns a copy of the mesh data and a BVH for fast proximity queries.
/// For animated meshes, call updateVertices() each frame to refit the BVH.
class ClothMeshCollider
{
public:
    /// @brief Builds the collider from triangle mesh data.
    /// @param vertices Vertex positions (copied).
    /// @param vertexCount Number of vertices.
    /// @param indices Triangle indices (copied). Must be multiple of 3.
    /// @param indexCount Number of indices.
    void build(const glm::vec3* vertices, size_t vertexCount,
               const uint32_t* indices, size_t indexCount);

    /// @brief Updates vertex positions and refits the BVH (O(N)).
    /// @param newPositions Updated vertex positions (same count as build).
    /// @param count Must match original vertexCount.
    void updateVertices(const glm::vec3* newPositions, size_t count);

    /// @brief Finds the closest point on the mesh to the query point.
    /// @param point Query point in world space.
    /// @param maxDist Maximum search distance.
    /// @param outPoint Closest point on the mesh surface.
    /// @param outNormal Surface normal at closest point (faces query point).
    /// @param outDist Distance from query point to closest point.
    /// @return true if a point was found within maxDist.
    bool queryClosest(const glm::vec3& point, float maxDist,
                      glm::vec3& outPoint, glm::vec3& outNormal,
                      float& outDist) const;

    /// @brief Returns the number of triangles in the mesh.
    size_t getTriangleCount() const;

    /// @brief Returns the number of vertices in the mesh.
    size_t getVertexCount() const;

    /// @brief Returns true if the collider has been built.
    bool isBuilt() const;

    /// @brief Returns a pointer to the vertex positions array.
    const glm::vec3* getVertices() const;

    /// @brief Returns a pointer to the triangle indices array.
    const uint32_t* getIndices() const;

private:
    std::vector<glm::vec3> m_vertices;
    std::vector<uint32_t> m_indices;
    BVH m_bvh;
};

} // namespace Vestige
