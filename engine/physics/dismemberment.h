/// @file dismemberment.h
/// @brief Runtime mesh splitting and dismemberment system.
#pragma once

#include "physics/dismemberment_zones.h"
#include "renderer/mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief Result of splitting a skinned mesh at a bone boundary.
struct SplitResult
{
    // Body side (stays attached)
    std::vector<Vertex> bodyVertices;
    std::vector<uint32_t> bodyIndices;

    // Severed limb
    std::vector<Vertex> limbVertices;
    std::vector<uint32_t> limbIndices;

    // Cap geometry (cross-section at the cut)
    std::vector<Vertex> capVertices;
    std::vector<uint32_t> capIndices;

    // Physics properties for the severed part
    glm::vec3 limbCentroid = glm::vec3(0.0f);
    float limbVolume = 0.0f;

    bool success = false;
};

/// @brief Performs runtime mesh splitting for dismemberment.
///
/// Classifies vertices by bone weight dominance, splits triangles
/// that straddle the cut boundary, and generates cap geometry for
/// the cross-section.
class Dismemberment
{
public:
    /// @brief Splits a skinned mesh at a bone boundary defined by a zone.
    /// @param vertices Source mesh vertices (with bone weights).
    /// @param indices Source mesh triangle indices.
    /// @param zone The dismemberment zone defining the cut.
    /// @param skeleton The skeleton for bone hierarchy traversal.
    /// @param boneMatrices Current bone world matrices (for cut plane positioning).
    /// @return Split result with body mesh, limb mesh, and cap mesh.
    static SplitResult splitAtBone(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const DismembermentZone& zone,
        const Skeleton& skeleton,
        const std::vector<glm::mat4>& boneMatrices);

    /// @brief Classifies vertices by bone weight dominance relative to a cut bone.
    ///
    /// Each vertex is classified as body-side (0) or limb-side (1) based on
    /// whether its dominant bone weight belongs to the severed bone or its children.
    ///
    /// @param vertices Source vertices with bone weights.
    /// @param cutBoneIndex Index of the bone being severed.
    /// @param skeleton Skeleton for hierarchy traversal.
    /// @param weightThreshold Minimum weight to consider a bone "dominant" (default 0.1).
    /// @param outSide Output: 0 = body side, 1 = limb side (one per vertex).
    static void classifyVertices(
        const std::vector<Vertex>& vertices,
        int cutBoneIndex,
        const Skeleton& skeleton,
        float weightThreshold,
        std::vector<int>& outSide);

private:
    /// @brief Checks if a bone is a child (descendant) of another bone.
    static bool isBoneChildOf(const Skeleton& skeleton, int boneIndex, int parentBoneIndex);

    /// @brief Interpolates a vertex between two vertices at parameter t.
    static Vertex interpolateVertex(const Vertex& a, const Vertex& b, float t);

    /// @brief Generates cap geometry from boundary edge loops.
    static void generateCapMesh(
        const std::vector<glm::vec3>& edgePoints,
        const glm::vec3& cutPlaneNormal,
        const glm::vec3& cutPlaneCenter,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices);
};

} // namespace Vestige
