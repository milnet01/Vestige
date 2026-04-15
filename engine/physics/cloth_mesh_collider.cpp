// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_mesh_collider.cpp
/// @brief Triangle mesh collider implementation.
#include "physics/cloth_mesh_collider.h"

#include <algorithm>

namespace Vestige
{

void ClothMeshCollider::build(const glm::vec3* vertices, size_t vertexCount,
                               const uint32_t* indices, size_t indexCount)
{
    if (vertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount < 3)
    {
        return;
    }

    m_vertices.assign(vertices, vertices + vertexCount);
    m_indices.assign(indices, indices + indexCount);
    m_bvh.build(m_vertices.data(), m_vertices.size(),
                m_indices.data(), m_indices.size());
}

void ClothMeshCollider::updateVertices(const glm::vec3* newPositions, size_t count)
{
    if (newPositions == nullptr || count != m_vertices.size())
    {
        return;
    }

    std::copy(newPositions, newPositions + count, m_vertices.begin());
    m_bvh.refit(m_vertices.data(), m_indices.data());
}

bool ClothMeshCollider::queryClosest(const glm::vec3& point, float maxDist,
                                      glm::vec3& outPoint, glm::vec3& outNormal,
                                      float& outDist) const
{
    BVHQueryResult result;
    if (!m_bvh.queryClosest(point, maxDist, m_vertices.data(), m_indices.data(), result))
    {
        return false;
    }

    outPoint = result.closestPoint;
    outNormal = result.normal;
    outDist = result.distance;
    return true;
}

size_t ClothMeshCollider::getTriangleCount() const
{
    return m_indices.size() / 3;
}

size_t ClothMeshCollider::getVertexCount() const
{
    return m_vertices.size();
}

bool ClothMeshCollider::isBuilt() const
{
    return m_bvh.isBuilt();
}

const glm::vec3* ClothMeshCollider::getVertices() const
{
    return m_vertices.data();
}

const uint32_t* ClothMeshCollider::getIndices() const
{
    return m_indices.data();
}

} // namespace Vestige
