// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tilemap_component.h
/// @brief TilemapComponent — multi-layer tile grid + animated tiles
/// (Phase 9F-3).
///
/// The tilemap addresses tiles by integer ID into a shared SpriteAtlas;
/// tile 0 is always "empty" so unpainted cells don't draw. Tiles can be
/// animated — a tile ID resolves to a sequence of atlas frames with a
/// common per-frame duration, ticked globally across the tilemap (all
/// "water" tiles advance in sync, which is the look designers expect).
#pragma once

#include "scene/component.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

class SpriteAtlas;

using TileId = std::uint16_t;
constexpr TileId kEmptyTile = 0;

/// @brief One layer of a tilemap — a dense grid of tile IDs.
struct TilemapLayer
{
    std::string name = "Layer";
    int         width  = 0;  ///< Columns.
    int         height = 0;  ///< Rows.
    int         zOrder = 0;  ///< Sort order within the tilemap.
    std::vector<TileId> tiles;  ///< width*height entries, row-major, bottom row first.

    /// @brief Resizes the grid. Existing overlap is preserved; new cells
    /// are zeroed. width*height overflow on 16-bit multiply is defended
    /// against by clamping dimensions to 0x7FFF.
    void resize(int newWidth, int newHeight);

    /// @brief Returns the tile at (col, row) — row 0 is the bottom row.
    /// Out-of-bounds cells return @ref kEmptyTile.
    TileId get(int col, int row) const;

    /// @brief Sets the tile at (col, row). No-op for out-of-bounds cells.
    void set(int col, int row, TileId id);

    /// @brief Returns a span-like view of all tile IDs (row-major).
    const std::vector<TileId>& cells() const { return tiles; }
};

/// @brief Definition of a single animated-tile sequence.
struct TilemapAnimatedTile
{
    TileId                   firstTileId = 0;   ///< The "base" tile ID that animates.
    std::vector<std::string> frames;            ///< Atlas frame names, in order.
    float                    framePeriodSec = 0.25f;  ///< Duration of each frame.
    bool                     pingPong = false;
};

/// @brief Per-tile lookup — converts an authored TileId into an atlas
/// frame name. The mapping is 1-to-1 for static tiles and many-to-one
/// for animated tiles (multiple TileIds collapse into the same base
/// animation).
struct TilemapTileDefinition
{
    std::string atlasFrameName;       ///< Primary static frame.
    bool        isAnimated = false;
    std::size_t animationIndex = 0;   ///< Index into TilemapComponent::animations.
};

/// @brief A tilemap attached to an entity. Tilemap origin lives at the
/// entity's world position; each cell is `tileWorldSize` units on a side.
class TilemapComponent : public Component
{
public:
    TilemapComponent() = default;
    ~TilemapComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    /// @brief Atlas whose frames the tile IDs index into.
    std::shared_ptr<SpriteAtlas> atlas;

    /// @brief Tile → atlas frame dictionary. Indexed by tile ID. Entry
    /// 0 is always empty (authored tiles start at ID 1).
    std::vector<TilemapTileDefinition> tileDefs;

    /// @brief Animated-tile sequences referenced by `tileDefs`.
    std::vector<TilemapAnimatedTile> animations;

    /// @brief Worldspace size of a single cell (same units as SpriteComponent).
    float tileWorldSize = 1.0f;

    /// @brief Pixels per world unit for rendering the tile sprites.
    float pixelsPerUnit = 100.0f;

    /// @brief Layers bottom-to-top by z.
    std::vector<TilemapLayer> layers;

    /// @brief Coarse sort key (matches SpriteComponent::sortingLayer).
    int sortingLayer = 0;

    /// @brief Fine sort key inside the layer (below the individual
    /// tile's zOrder).
    int orderInLayer = 0;

    // ---- Runtime (per-frame) — updated by TilemapComponent::update ----

    /// @brief Global animation timer — wraps at a large period so every
    /// animated tile's mod is stable regardless of scene duration.
    float animationElapsedSec = 0.0f;

    // ---- Convenience ----

    /// @brief Adds a new empty layer. Returns its index.
    std::size_t addLayer(const std::string& name, int width, int height);

    /// @brief Looks up the atlas-frame name to render for a given tile
    /// ID at the current animation time. Returns empty string for
    /// `kEmptyTile` or malformed input.
    std::string resolveFrameName(TileId id) const;

    /// @brief Enumerates every non-empty cell in draw order. Each
    /// callback receives (layer index, col, row, resolved frame name).
    /// Designed so both the renderer and unit tests can drive a tilemap
    /// without duplicating the walk. Stops early if @p fn returns false.
    void forEachVisibleTile(const std::function<bool(std::size_t, int, int,
                                                     const std::string&)>& fn) const;
};

} // namespace Vestige
