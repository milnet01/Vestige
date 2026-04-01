/// @file bvh.h
/// @brief AABB Bounding Volume Hierarchy for triangle mesh proximity queries.
///
/// Used by ClothMeshCollider for cloth-body collision detection.
/// Construction uses binned SAH (Surface Area Heuristic) with 8 bins.
/// Supports O(N) bottom-up refit for animated meshes.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief Result of a BVH proximity query.
struct BVHQueryResult
{
    size_t triangleIndex = 0;   ///< Index of the closest triangle
    glm::vec3 closestPoint{};   ///< Closest point on the triangle surface
    glm::vec3 normal{};         ///< Surface normal at the closest point (faces query point)
    float distance = 0.0f;      ///< Distance from query point to closest point
};

/// @brief AABB-based Bounding Volume Hierarchy for triangle meshes.
///
/// Provides fast point-proximity queries for cloth collision detection.
/// Tree is built once with SAH, then refitted each frame for animated meshes.
class BVH
{
public:
    /// @brief Builds the BVH from triangle mesh data.
    /// @param vertices Vertex position array.
    /// @param vertexCount Number of vertices.
    /// @param indices Triangle index array (3 indices per triangle, CCW).
    /// @param indexCount Number of indices (must be multiple of 3).
    void build(const glm::vec3* vertices, size_t vertexCount,
               const uint32_t* indices, size_t indexCount);

    /// @brief Refits all AABBs using updated vertex positions (same topology).
    /// @param vertices Updated vertex positions (same count as build).
    /// @param indices Triangle index array (same as build).
    void refit(const glm::vec3* vertices, const uint32_t* indices);

    /// @brief Finds the closest triangle to a query point within maxDist.
    /// @param point Query point in world space.
    /// @param maxDist Maximum search distance (typically collision margin).
    /// @param vertices Current vertex positions.
    /// @param indices Triangle index array (same as build).
    /// @param result Output: closest triangle info.
    /// @return true if a triangle was found within maxDist.
    bool queryClosest(const glm::vec3& point, float maxDist,
                      const glm::vec3* vertices, const uint32_t* indices,
                      BVHQueryResult& result) const;

    /// @brief Returns the number of BVH nodes.
    size_t getNodeCount() const;

    /// @brief Returns the number of triangles in the BVH.
    size_t getTriangleCount() const;

    /// @brief Returns true if the BVH has been built.
    bool isBuilt() const;

    /// @brief Computes the closest point on triangle (a, b, c) to point p.
    /// Uses Ericson's Voronoi region method (Real-Time Collision Detection, Ch. 5).
    static glm::vec3 closestPointOnTriangle(const glm::vec3& p,
                                             const glm::vec3& a,
                                             const glm::vec3& b,
                                             const glm::vec3& c);

private:
    /// @brief Internal BVH node (leaf or internal).
    struct Node
    {
        glm::vec3 boundsMin{};
        glm::vec3 boundsMax{};
        int32_t leftChild = -1;    ///< -1 for leaf nodes
        int32_t rightChild = -1;   ///< -1 for leaf nodes
        int32_t triStart = 0;      ///< Start index in m_sortedTriIndices
        int32_t triCount = 0;      ///< Number of triangles (>0 for leaves)

        bool isLeaf() const { return leftChild < 0; }
    };

    /// @brief Triangle info used during construction.
    struct TriInfo
    {
        glm::vec3 centroid{};
        glm::vec3 boundsMin{};
        glm::vec3 boundsMax{};
        uint32_t originalIndex = 0;
    };

    std::vector<Node> m_nodes;
    std::vector<uint32_t> m_sortedTriIndices;  ///< Triangle indices in leaf order
    size_t m_triCount = 0;

    static constexpr int MAX_LEAF_TRIS = 4;
    static constexpr int NUM_BINS = 8;

    int buildRecursive(int start, int end, std::vector<TriInfo>& tris);
    void findBestSplit(int start, int end, const std::vector<TriInfo>& tris,
                       const glm::vec3& nodeMin, const glm::vec3& nodeMax,
                       int& bestAxis, float& bestPos, float& bestCost) const;

    static float surfaceArea(const glm::vec3& bMin, const glm::vec3& bMax);
    static float distToAABB(const glm::vec3& point,
                            const glm::vec3& bMin, const glm::vec3& bMax);
};

} // namespace Vestige
