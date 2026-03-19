/// @file frustum.h
/// @brief Frustum plane extraction and AABB-vs-frustum culling utilities.
#pragma once

#include "utils/aabb.h"

#include <glm/glm.hpp>

#include <array>

namespace Vestige
{

/// @brief A set of 6 planes defining a view frustum.
/// Each plane is (a, b, c, d) where ax + by + cz + d >= 0 is inside.
using FrustumPlanes = std::array<glm::vec4, 6>;

/// @brief Extracts the 6 frustum planes from a view-projection matrix.
/// Uses the Gribb-Hartmann method for standard [-1, 1] NDC.
/// @param vp Combined view-projection matrix.
/// @return Array of 6 normalized planes: left, right, bottom, top, near, far.
inline FrustumPlanes extractFrustumPlanes(const glm::mat4& vp)
{
    FrustumPlanes planes;
    // Left
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                           vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    // Right
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                           vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    // Bottom
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                           vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    // Top
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                           vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    // Near
    planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                           vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
    // Far
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                           vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    // Normalize each plane
    for (auto& p : planes)
    {
        float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
    return planes;
}

/// @brief Tests if an AABB is at least partially inside the frustum.
/// Uses the p-vertex test: for each plane, check the AABB corner most
/// aligned with the plane normal. If any plane rejects it, the box is outside.
/// @param box World-space axis-aligned bounding box.
/// @param planes 6 frustum planes from extractFrustumPlanes().
/// @return True if the AABB is at least partially inside.
inline bool isAabbInFrustum(const AABB& box, const FrustumPlanes& planes)
{
    for (const auto& plane : planes)
    {
        // Find the AABB corner most aligned with the plane normal (p-vertex)
        glm::vec3 pVertex(
            (plane.x >= 0.0f) ? box.max.x : box.min.x,
            (plane.y >= 0.0f) ? box.max.y : box.min.y,
            (plane.z >= 0.0f) ? box.max.z : box.min.z
        );

        if (glm::dot(glm::vec3(plane), pVertex) + plane.w < 0.0f)
        {
            return false;  // Entirely outside this plane
        }
    }
    return true;
}

} // namespace Vestige
