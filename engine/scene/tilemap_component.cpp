// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tilemap_component.cpp
/// @brief TilemapComponent implementation.
#include "scene/tilemap_component.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

void TilemapLayer::resize(int newWidth, int newHeight)
{
    // Clamp to a safe 2^15 cap so width*height fits in 32-bit without
    // overflow concerns — beyond that, the scene would dwarf any real
    // game's needs and rendering a 32767-wide layer per frame is
    // untenable anyway.
    newWidth  = std::clamp(newWidth,  0, 0x7FFF);
    newHeight = std::clamp(newHeight, 0, 0x7FFF);

    std::vector<TileId> next(static_cast<std::size_t>(newWidth) *
                             static_cast<std::size_t>(newHeight),
                             kEmptyTile);
    const int copyW = std::min(width, newWidth);
    const int copyH = std::min(height, newHeight);
    for (int r = 0; r < copyH; ++r)
    {
        for (int c = 0; c < copyW; ++c)
        {
            next[static_cast<std::size_t>(r * newWidth + c)] =
                tiles[static_cast<std::size_t>(r * width + c)];
        }
    }
    tiles = std::move(next);
    width = newWidth;
    height = newHeight;
}

TileId TilemapLayer::get(int col, int row) const
{
    if (col < 0 || col >= width || row < 0 || row >= height)
    {
        return kEmptyTile;
    }
    return tiles[static_cast<std::size_t>(row * width + col)];
}

void TilemapLayer::set(int col, int row, TileId id)
{
    if (col < 0 || col >= width || row < 0 || row >= height)
    {
        return;
    }
    tiles[static_cast<std::size_t>(row * width + col)] = id;
}

void TilemapComponent::update(float deltaTime)
{
    if (!m_isEnabled)
    {
        return;
    }
    animationElapsedSec += deltaTime;
    // Keep the timer bounded so animations stay numerically stable even
    // in a scene that runs for days. Wrap at 1 hour — long enough to
    // span a real gameplay session, short enough that float precision
    // stays tight around the fractional duration.
    constexpr float kWrap = 3600.0f;
    if (animationElapsedSec > kWrap)
    {
        animationElapsedSec = std::fmod(animationElapsedSec, kWrap);
    }
}

std::unique_ptr<Component> TilemapComponent::clone() const
{
    auto c = std::make_unique<TilemapComponent>();
    c->atlas         = atlas;        // shared atlas
    c->tileDefs      = tileDefs;
    c->animations    = animations;
    c->tileWorldSize = tileWorldSize;
    c->pixelsPerUnit = pixelsPerUnit;
    c->layers        = layers;
    c->sortingLayer  = sortingLayer;
    c->orderInLayer  = orderInLayer;
    // Animation time intentionally NOT cloned — clones start at 0 so
    // duplicates don't inherit a mid-cycle offset.
    c->animationElapsedSec = 0.0f;
    c->setEnabled(m_isEnabled);
    return c;
}

std::size_t TilemapComponent::addLayer(const std::string& name, int width, int height)
{
    TilemapLayer layer;
    layer.name = name;
    layer.resize(width, height);
    layers.push_back(std::move(layer));
    return layers.size() - 1;
}

std::string TilemapComponent::resolveFrameName(TileId id) const
{
    if (id == kEmptyTile || id >= tileDefs.size())
    {
        return {};
    }
    const auto& def = tileDefs[id];
    if (!def.isAnimated || def.animationIndex >= animations.size())
    {
        return def.atlasFrameName;
    }
    const auto& anim = animations[def.animationIndex];
    if (anim.frames.empty() || anim.framePeriodSec <= 0.0f)
    {
        return def.atlasFrameName;
    }
    const float t = animationElapsedSec / anim.framePeriodSec;
    const std::size_t n = anim.frames.size();

    std::size_t idx = 0;
    if (anim.pingPong && n > 1)
    {
        // Ping-pong period: 2*(n-1) frames, reflecting at both ends.
        const std::size_t period = (n - 1) * 2;
        const std::size_t step = static_cast<std::size_t>(t) % period;
        idx = step < n ? step : period - step;
    }
    else
    {
        idx = static_cast<std::size_t>(t) % n;
    }
    return anim.frames[idx];
}

void TilemapComponent::forEachVisibleTile(
    const std::function<bool(std::size_t, int, int, const std::string&)>& fn) const
{
    for (std::size_t li = 0; li < layers.size(); ++li)
    {
        const auto& layer = layers[li];
        for (int r = 0; r < layer.height; ++r)
        {
            for (int c = 0; c < layer.width; ++c)
            {
                const TileId id = layer.get(c, r);
                if (id == kEmptyTile)
                {
                    continue;
                }
                const std::string frame = resolveFrameName(id);
                if (frame.empty())
                {
                    continue;
                }
                if (!fn(li, c, r, frame))
                {
                    return;
                }
            }
        }
    }
}

} // namespace Vestige
