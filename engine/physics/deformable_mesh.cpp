// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file deformable_mesh.cpp
/// @brief Mesh deformation utilities implementation.

#include "physics/deformable_mesh.h"

#include <cmath>

namespace Vestige
{

void DeformableMesh::applyImpact(
    std::vector<Vertex>& vertices,
    const glm::vec3& impactPoint,
    const glm::vec3& impactDirection,
    float radius,
    float depth)
{
    if (radius <= 0.0f || depth <= 0.0f)
        return;

    float dirLen = glm::length(impactDirection);
    if (dirLen < 0.0001f)
        return;
    glm::vec3 dir = impactDirection / dirLen;
    float radiusSq = radius * radius;

    for (auto& vertex : vertices)
    {
        glm::vec3 delta = vertex.position - impactPoint;
        float distSq = glm::dot(delta, delta);

        if (distSq >= radiusSq)
            continue;

        // Smooth falloff: (1 - (d/r)^2)^2
        float t = distSq / radiusSq;
        float falloff = (1.0f - t) * (1.0f - t);

        // Displace vertex along impact direction
        vertex.position += dir * (depth * falloff);
    }
}

void DeformableMesh::recalculateNormals(
    std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    // Zero all normals
    for (auto& v : vertices)
    {
        v.normal = glm::vec3(0.0f);
    }

    // Accumulate face normals
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;

        glm::vec3 edge1 = vertices[i1].position - vertices[i0].position;
        glm::vec3 edge2 = vertices[i2].position - vertices[i0].position;
        glm::vec3 faceNormal = glm::cross(edge1, edge2);

        vertices[i0].normal += faceNormal;
        vertices[i1].normal += faceNormal;
        vertices[i2].normal += faceNormal;
    }

    // Normalize
    for (auto& v : vertices)
    {
        float len = glm::length(v.normal);
        if (len > 0.0001f)
        {
            v.normal /= len;
        }
    }
}

} // namespace Vestige
