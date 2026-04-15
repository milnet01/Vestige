// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file morph_target.cpp
/// @brief Morph target CPU blending implementation.
#include "animation/morph_target.h"

#include <algorithm>

namespace Vestige
{

void blendMorphPositions(const MorphTargetData& data,
                         const std::vector<float>& weights,
                         const std::vector<glm::vec3>& basePositions,
                         std::vector<glm::vec3>& outPositions)
{
    size_t vertCount = basePositions.size();
    outPositions.resize(vertCount);

    // Start with base positions
    std::copy(basePositions.begin(), basePositions.end(), outPositions.begin());

    // Accumulate weighted deltas from each target
    size_t targetCount = data.targetCount();
    size_t weightCount = weights.size();

    for (size_t t = 0; t < targetCount && t < weightCount; ++t)
    {
        float w = weights[t];
        if (w == 0.0f) continue;

        const auto& deltas = data.targets[t].positionDeltas;
        size_t count = std::min(vertCount, deltas.size());

        for (size_t v = 0; v < count; ++v)
        {
            outPositions[v] += deltas[v] * w;
        }
    }
}

void blendMorphNormals(const MorphTargetData& data,
                       const std::vector<float>& weights,
                       const std::vector<glm::vec3>& baseNormals,
                       std::vector<glm::vec3>& outNormals)
{
    size_t vertCount = baseNormals.size();
    outNormals.resize(vertCount);

    std::copy(baseNormals.begin(), baseNormals.end(), outNormals.begin());

    size_t targetCount = data.targetCount();
    size_t weightCount = weights.size();

    for (size_t t = 0; t < targetCount && t < weightCount; ++t)
    {
        float w = weights[t];
        if (w == 0.0f) continue;

        const auto& deltas = data.targets[t].normalDeltas;
        if (deltas.empty()) continue;

        size_t count = std::min(vertCount, deltas.size());
        for (size_t v = 0; v < count; ++v)
        {
            outNormals[v] += deltas[v] * w;
        }
    }
}

} // namespace Vestige
