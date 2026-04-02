/// @file fracture.h
/// @brief Voronoi fracture algorithm for dynamic destruction.
#pragma once

#include "utils/aabb.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief A single fragment produced by Voronoi fracture.
struct FractureFragment
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t> indices;
    std::vector<bool> isInteriorFace;  ///< True for faces created by the cut
    glm::vec3 centroid = glm::vec3(0.0f);
    float volume = 0.0f;
};

/// @brief Result of a Voronoi fracture operation.
struct FractureResult
{
    std::vector<FractureFragment> fragments;
    bool success = false;
};

/// @brief Voronoi-based mesh fracture algorithm.
///
/// Splits a convex mesh into multiple fragments using Voronoi cells.
/// Seed points are biased toward the impact location for realistic
/// fracture patterns (more small fragments near impact, fewer large
/// fragments far away).
class Fracture
{
public:
    /// @brief Fractures a convex mesh into fragments using Voronoi decomposition.
    /// @param vertices Mesh vertex positions.
    /// @param indices Triangle indices.
    /// @param fragmentCount Number of fragments to produce.
    /// @param impactPoint World-space point of impact (seeds biased toward this).
    /// @param seed Random seed for reproducible results.
    /// @return Fracture result containing fragments.
    static FractureResult fractureConvex(
        const std::vector<glm::vec3>& vertices,
        const std::vector<uint32_t>& indices,
        int fragmentCount,
        const glm::vec3& impactPoint,
        uint32_t seed = 0);

    /// @brief Generates Voronoi seed points biased toward the impact location.
    /// @param bounds Bounding box of the mesh.
    /// @param count Number of seed points to generate.
    /// @param impactBias Impact point (more seeds placed near here).
    /// @param seed Random seed.
    /// @return Generated seed points within the bounding box.
    static std::vector<glm::vec3> generateSeeds(
        const AABB& bounds,
        int count,
        const glm::vec3& impactBias,
        uint32_t seed);

    /// @brief Computes the volume of a triangle mesh using the divergence theorem.
    /// @param positions Vertex positions.
    /// @param indices Triangle indices.
    /// @return Signed volume (positive for outward-facing normals).
    static float computeVolume(
        const std::vector<glm::vec3>& positions,
        const std::vector<uint32_t>& indices);

    /// @brief Computes the centroid of a set of points.
    static glm::vec3 computeCentroid(const std::vector<glm::vec3>& points);

private:
    /// @brief Clips a convex polyhedron (represented as a set of vertices)
    /// against a half-plane.
    /// @param vertices Input convex vertices.
    /// @param planePoint A point on the clipping plane.
    /// @param planeNormal Outward normal of the clipping plane.
    /// @return Vertices on the positive side of the plane (or on the plane).
    static std::vector<glm::vec3> clipConvexByPlane(
        const std::vector<glm::vec3>& vertices,
        const glm::vec3& planePoint,
        const glm::vec3& planeNormal);

    /// @brief Triangulates a convex polygon (fan triangulation).
    /// @param polygon Ordered polygon vertices.
    /// @param outPositions Output positions.
    /// @param outNormals Output normals.
    /// @param outUvs Output UVs.
    /// @param outIndices Output indices.
    /// @param isInterior Whether this face is an interior (cut) face.
    /// @param outInterior Output interior flags.
    static void triangulateFace(
        const std::vector<glm::vec3>& polygon,
        const glm::vec3& faceNormal,
        std::vector<glm::vec3>& outPositions,
        std::vector<glm::vec3>& outNormals,
        std::vector<glm::vec2>& outUvs,
        std::vector<uint32_t>& outIndices,
        bool isInterior,
        std::vector<bool>& outInterior);

    /// @brief Extracts and triangulates faces from a convex hull.
    static void buildConvexHullMesh(
        const std::vector<glm::vec3>& hullPoints,
        FractureFragment& outFragment,
        const std::vector<glm::vec3>& cellVertices);
};

} // namespace Vestige
