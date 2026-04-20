// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tilemap_renderer.h
/// @brief TilemapRenderer — converts a TilemapComponent into
/// SpriteInstance records for the shared SpriteRenderer (Phase 9F-3).
///
/// The tilemap is drawn as ordinary sprites: for every non-empty cell
/// in every layer, emit one instance with the atlas frame corresponding
/// to the cell's tile id. Animated tiles resolve to the frame the
/// tilemap's current animation time picks. No dedicated shader or VBO —
/// tilemaps live inside the same batched pass as SpriteComponents, so a
/// scene with sprites and tilemaps gets a single draw path and a single
/// depth ordering.
///
/// The renderer is a pure helper: input is a tilemap + its world matrix,
/// output is a vector of SpriteInstance that the SpriteRenderer can feed
/// through `drawBatch`. Separating the data path from GL makes the
/// conversion testable without a context.
#pragma once

#include "renderer/sprite_renderer.h"

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

class TilemapComponent;
class SpriteAtlas;

/// @brief Converts every visible tile in @p tilemap into SpriteInstance
/// records and appends them to @p outInstances. Returns the atlas the
/// instances are bound to (nullptr if the tilemap has no atlas or no
/// visible tiles).
///
/// The tilemap origin sits at the entity's world position; each tile is
/// laid out in the XY plane with column 0 / row 0 at the origin and
/// increasing col → +X, increasing row → +Y.
///
/// @param tilemap     The source tilemap.
/// @param worldMatrix Parent entity's world transform (position taken
///                    from the translation column; rotation is
///                    ignored — tilemaps are axis-aligned by design).
/// @param depth       Z-buffer depth written into every instance
///                    (maps to gl_Position.z via the sprite shader).
/// @param outInstances Vector to append to. Not cleared.
const SpriteAtlas* buildTilemapInstances(
    const TilemapComponent& tilemap,
    const glm::mat4& worldMatrix,
    float depth,
    std::vector<SpriteInstance>& outInstances);

} // namespace Vestige
