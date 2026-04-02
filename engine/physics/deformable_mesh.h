/// @file deformable_mesh.h
/// @brief Mesh deformation utilities (denting, impact damage).
#pragma once

#include "renderer/mesh.h"

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief Utilities for applying visual deformation to mesh vertex data.
///
/// Used for pre-fracture denting (showing impact damage before the object
/// actually breaks apart).
class DeformableMesh
{
public:
    /// @brief Applies impact deformation (denting) to vertex positions.
    ///
    /// Vertices within the radius of the impact point are pushed inward
    /// along the impact direction with a smooth falloff.
    ///
    /// @param vertices Vertex data to modify in-place.
    /// @param impactPoint World-space impact location.
    /// @param impactDirection Direction of the impact force (normalized).
    /// @param radius Radius of the deformation area.
    /// @param depth Maximum displacement depth.
    static void applyImpact(
        std::vector<Vertex>& vertices,
        const glm::vec3& impactPoint,
        const glm::vec3& impactDirection,
        float radius,
        float depth);

    /// @brief Recomputes smooth normals for the deformed mesh.
    /// @param vertices Vertex data to update normals for.
    /// @param indices Triangle indices.
    static void recalculateNormals(
        std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices);
};

} // namespace Vestige
