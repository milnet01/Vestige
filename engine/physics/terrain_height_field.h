// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_height_field.h
/// @brief Builds a Jolt height-field collider from a terrain's heightmap.
#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief A square heightmap in the terrain's own units. Plain data, so the
/// physics module needs no Terrain (and no GL context) to build the collider.
struct TerrainHeightFieldDesc
{
    std::vector<float> heights01;  ///< sampleCount² normalised heights, row-major (z * sampleCount + x)
    int sampleCount = 0;           ///< Samples along each side
    glm::vec3 origin{0.0f};        ///< World position of sample (0, 0); y is ignored
    float spacingX = 1.0f;         ///< World metres between samples along X
    float spacingZ = 1.0f;         ///< World metres between samples along Z
    float heightScale = 1.0f;      ///< World height of a normalised 1.0
};

/// @brief Creates a static height-field shape whose surface at sample (x, z)
/// is world (origin.x + x * spacingX, heights01 * heightScale,
/// origin.z + z * spacingZ) — the same heights Terrain::getHeight returns.
/// @return The shape, or null when the desc is malformed or Jolt refuses it.
JPH::ShapeRefC createTerrainHeightFieldShape(const TerrainHeightFieldDesc& desc);

} // namespace Vestige
