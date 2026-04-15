// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file morph_target.h
/// @brief Morph target (blend shape) data structures and CPU blending.
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Displacement data for a single morph target.
///
/// Stores per-vertex position/normal/tangent deltas relative to the base mesh.
/// glTF morph targets are displacement-based: blendedPos = basePos + sum(weight[i] * delta[i]).
struct MorphTarget
{
    std::string name;                          ///< Optional target name (e.g. "smile", "blink_L")
    std::vector<glm::vec3> positionDeltas;     ///< Per-vertex position offsets
    std::vector<glm::vec3> normalDeltas;       ///< Per-vertex normal offsets (optional, may be empty)
    std::vector<glm::vec3> tangentDeltas;      ///< Per-vertex tangent offsets (optional, may be empty)
};

/// @brief Collection of morph targets for a single mesh primitive.
struct MorphTargetData
{
    std::vector<MorphTarget> targets;          ///< All morph targets for this mesh
    std::vector<float> defaultWeights;         ///< Default weights from glTF mesh.weights
    size_t vertexCount = 0;                    ///< Must match the base mesh vertex count

    /// @brief Returns the number of morph targets.
    size_t targetCount() const { return targets.size(); }

    /// @brief Checks if this data has any morph targets.
    bool empty() const { return targets.empty(); }
};

/// @brief Computes blended vertex positions from base mesh + morph target weights.
///
/// result[i] = basePositions[i] + sum_over_j(weights[j] * targets[j].positionDeltas[i])
///
/// @param data Morph target data containing all targets.
/// @param weights Active weights (one per target). Length must match data.targetCount().
/// @param basePositions Base mesh vertex positions.
/// @param outPositions Output: blended positions. Resized to match basePositions.
void blendMorphPositions(const MorphTargetData& data,
                         const std::vector<float>& weights,
                         const std::vector<glm::vec3>& basePositions,
                         std::vector<glm::vec3>& outPositions);

/// @brief Computes blended vertex normals from base mesh + morph target weights.
///
/// Same formula as positions. Only processes targets that have normalDeltas.
///
/// @param data Morph target data.
/// @param weights Active weights.
/// @param baseNormals Base mesh vertex normals.
/// @param outNormals Output: blended normals (not renormalized).
void blendMorphNormals(const MorphTargetData& data,
                       const std::vector<float>& weights,
                       const std::vector<glm::vec3>& baseNormals,
                       std::vector<glm::vec3>& outNormals);

} // namespace Vestige
