// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file collider_generator.cpp
/// @brief Collider generation utility implementation.
#include "physics/collider_generator.h"

namespace Vestige
{

ClothMeshCollider ColliderGenerator::fromMesh(const glm::vec3* positions, size_t vertexCount,
                                               const uint32_t* indices, size_t indexCount)
{
    ClothMeshCollider collider;
    collider.build(positions, vertexCount, indices, indexCount);
    return collider;
}

} // namespace Vestige
