// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tilemap_renderer.cpp
/// @brief TilemapRenderer helper — tilemap → SpriteInstance list.
#include "renderer/tilemap_renderer.h"
#include "renderer/sprite_atlas.h"
#include "scene/tilemap_component.h"

namespace Vestige
{

const SpriteAtlas* buildTilemapInstances(
    const TilemapComponent& tilemap,
    const glm::mat4& worldMatrix,
    float depth,
    std::vector<SpriteInstance>& outInstances)
{
    const SpriteAtlas* atlas = tilemap.atlas.get();
    if (!atlas)
    {
        return nullptr;
    }

    const float tileSize = tilemap.tileWorldSize;
    const glm::vec3 origin(worldMatrix[3]);
    const glm::vec4 tint(1.0f);

    std::size_t addedBefore = outInstances.size();

    tilemap.forEachVisibleTile(
        [&](std::size_t /*layerIdx*/, int col, int row, const std::string& frame)
        {
            const auto* f = atlas->find(frame);
            if (!f)
            {
                return true;  // skip unresolved tile name; keep iterating
            }

            // Each tile is a unit-size quad in world space. The sprite
            // shader centres on (0.5, 0.5), so the affine translates the
            // tile's centre to (origin + (col + 0.5) * tileSize, origin + (row + 0.5) * tileSize).
            const float cx = origin.x + (static_cast<float>(col) + 0.5f) * tileSize;
            const float cy = origin.y + (static_cast<float>(row) + 0.5f) * tileSize;

            SpriteInstance inst{};
            // Scale = tileSize in X and Y, no rotation, centre at (cx, cy).
            inst.transformRow0 = glm::vec4(tileSize, 0.0f, cx, 0.0f);
            inst.transformRow1 = glm::vec4(0.0f, tileSize, cy, 0.0f);
            inst.uvRect = f->uv;
            inst.tint = tint;
            inst.depth = depth;
            outInstances.push_back(inst);
            return true;
        });

    return addedBefore == outInstances.size() ? nullptr : atlas;
}

} // namespace Vestige
