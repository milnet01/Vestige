/// @file aabb.h
/// @brief Axis-Aligned Bounding Box for collision detection.
#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <limits>

namespace Vestige
{

/// @brief An axis-aligned bounding box defined by min and max corners.
struct AABB
{
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);

    /// @brief Creates an AABB centered at origin with the given half-extents.
    static AABB fromCenterSize(const glm::vec3& center, const glm::vec3& size)
    {
        glm::vec3 halfSize = size * 0.5f;
        return {center - halfSize, center + halfSize};
    }

    /// @brief Creates a unit cube AABB (-0.5 to 0.5 on each axis).
    static AABB unitCube()
    {
        return {glm::vec3(-0.5f), glm::vec3(0.5f)};
    }

    /// @brief Checks if this AABB intersects another.
    bool intersects(const AABB& other) const
    {
        return (min.x <= other.max.x && max.x >= other.min.x)
            && (min.y <= other.max.y && max.y >= other.min.y)
            && (min.z <= other.max.z && max.z >= other.min.z);
    }

    /// @brief Checks if a point is inside this AABB.
    bool contains(const glm::vec3& point) const
    {
        return (point.x >= min.x && point.x <= max.x)
            && (point.y >= min.y && point.y <= max.y)
            && (point.z >= min.z && point.z <= max.z);
    }

    /// @brief Returns a new AABB transformed by a model matrix.
    /// @details Recomputes axis-aligned bounds from the 8 transformed corners.
    AABB transformed(const glm::mat4& matrix) const
    {
        glm::vec3 corners[8] = {
            glm::vec3(min.x, min.y, min.z),
            glm::vec3(max.x, min.y, min.z),
            glm::vec3(min.x, max.y, min.z),
            glm::vec3(max.x, max.y, min.z),
            glm::vec3(min.x, min.y, max.z),
            glm::vec3(max.x, min.y, max.z),
            glm::vec3(min.x, max.y, max.z),
            glm::vec3(max.x, max.y, max.z),
        };

        glm::vec3 newMin(std::numeric_limits<float>::max());
        glm::vec3 newMax(std::numeric_limits<float>::lowest());

        for (const auto& corner : corners)
        {
            glm::vec3 transformed = glm::vec3(matrix * glm::vec4(corner, 1.0f));
            newMin = glm::min(newMin, transformed);
            newMax = glm::max(newMax, transformed);
        }

        return {newMin, newMax};
    }

    /// @brief Gets the center point of the AABB.
    glm::vec3 getCenter() const
    {
        return (min + max) * 0.5f;
    }

    /// @brief Gets the size (width, height, depth) of the AABB.
    glm::vec3 getSize() const
    {
        return max - min;
    }

    /// @brief Calculates the minimum push vector to resolve an intersection.
    /// @param other The AABB we're colliding with.
    /// @return The smallest vector to move this AABB out of the other.
    glm::vec3 getMinPushOut(const AABB& other) const
    {
        float overlapX1 = max.x - other.min.x;
        float overlapX2 = other.max.x - min.x;
        float overlapY1 = max.y - other.min.y;
        float overlapY2 = other.max.y - min.y;
        float overlapZ1 = max.z - other.min.z;
        float overlapZ2 = other.max.z - min.z;

        float minOverlapX = (overlapX1 < overlapX2) ? -overlapX1 : overlapX2;
        float minOverlapY = (overlapY1 < overlapY2) ? -overlapY1 : overlapY2;
        float minOverlapZ = (overlapZ1 < overlapZ2) ? -overlapZ1 : overlapZ2;

        float absX = std::abs(minOverlapX);
        float absY = std::abs(minOverlapY);
        float absZ = std::abs(minOverlapZ);

        // Push along the axis with the smallest overlap
        if (absX <= absY && absX <= absZ)
        {
            return glm::vec3(minOverlapX, 0.0f, 0.0f);
        }
        else if (absY <= absX && absY <= absZ)
        {
            return glm::vec3(0.0f, minOverlapY, 0.0f);
        }
        else
        {
            return glm::vec3(0.0f, 0.0f, minOverlapZ);
        }
    }
};

} // namespace Vestige
