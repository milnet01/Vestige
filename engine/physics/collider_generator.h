/// @file collider_generator.h
/// @brief Utility to generate cloth collision shapes from mesh data.
#pragma once

#include "physics/cloth_mesh_collider.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Vestige
{

/// @brief Static utility for generating cloth colliders from mesh data.
class ColliderGenerator
{
public:
    /// @brief Creates a ClothMeshCollider from raw vertex/index data.
    /// @param positions Vertex positions array.
    /// @param vertexCount Number of vertices.
    /// @param indices Triangle indices (3 per triangle, CCW winding).
    /// @param indexCount Number of indices (must be multiple of 3).
    /// @return A built ClothMeshCollider ready for use.
    static ClothMeshCollider fromMesh(const glm::vec3* positions, size_t vertexCount,
                                       const uint32_t* indices, size_t indexCount);
};

} // namespace Vestige
