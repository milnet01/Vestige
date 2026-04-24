// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_tilemap.cpp
/// @brief Unit tests for TilemapComponent + TilemapRenderer (Phase 9F-3).
#include "renderer/sprite_atlas.h"
#include "renderer/tilemap_renderer.h"
#include "scene/tilemap_component.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

using namespace Vestige;

namespace
{

std::shared_ptr<SpriteAtlas> makeTileAtlas()
{
    // ctest runs tests in parallel as separate processes; suffix by test name
    // so each process writes to its own tiles.json instead of racing on a
    // shared path (intermittent Debug CI failure observed 2026-04-24).
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string key = info ? info->name() : "unknown";
    auto dir = std::filesystem::temp_directory_path() / ("vestige_tilemap_test_" + key);
    std::filesystem::create_directories(dir);
    const auto path = dir / "tiles.json";
    std::ofstream out(path);
    out << R"JSON({
      "frames": [
        { "filename": "grass",  "frame": {"x":0, "y":0, "w":32, "h":32}, "sourceSize": {"w":32,"h":32}},
        { "filename": "stone",  "frame": {"x":32,"y":0, "w":32, "h":32}, "sourceSize": {"w":32,"h":32}},
        { "filename": "water_0","frame": {"x":0, "y":32,"w":32, "h":32}, "sourceSize": {"w":32,"h":32}},
        { "filename": "water_1","frame": {"x":32,"y":32,"w":32, "h":32}, "sourceSize": {"w":32,"h":32}}
      ],
      "meta": {"image":"tiles.png", "size":{"w":64,"h":64}}
    })JSON";
    out.close();
    return SpriteAtlas::loadFromJson(path.string());
}

// Standard 4-tile palette: 0=empty, 1=grass, 2=stone, 3=water(animated).
TilemapComponent makeSampleTilemap(int w = 8, int h = 8)
{
    TilemapComponent tm;
    tm.atlas = makeTileAtlas();
    tm.tileDefs = {
        {},                                // 0: empty (never drawn)
        {"grass", false, 0},               // 1
        {"stone", false, 0},               // 2
        {"water_0", true, 0}               // 3: animated
    };
    TilemapAnimatedTile water;
    water.firstTileId = 3;
    water.frames = {"water_0", "water_1"};
    water.framePeriodSec = 0.5f;
    water.pingPong = false;
    tm.animations.push_back(water);
    tm.addLayer("base", w, h);
    return tm;
}

} // namespace

TEST(Tilemap, EmptyLayerAllCellsZero)
{
    auto tm = makeSampleTilemap(4, 4);
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            EXPECT_EQ(tm.layers[0].get(c, r), kEmptyTile);
        }
    }
}

TEST(Tilemap, SetAndGetRoundTrip)
{
    auto tm = makeSampleTilemap(4, 4);
    tm.layers[0].set(0, 0, 1);
    tm.layers[0].set(2, 3, 2);
    EXPECT_EQ(tm.layers[0].get(0, 0), 1);
    EXPECT_EQ(tm.layers[0].get(2, 3), 2);
    EXPECT_EQ(tm.layers[0].get(1, 1), kEmptyTile);
}

TEST(Tilemap, OutOfBoundsReturnsEmpty)
{
    auto tm = makeSampleTilemap(4, 4);
    tm.layers[0].set(0, 0, 1);
    EXPECT_EQ(tm.layers[0].get(-1, 0), kEmptyTile);
    EXPECT_EQ(tm.layers[0].get(4, 0), kEmptyTile);
    EXPECT_EQ(tm.layers[0].get(0, -1), kEmptyTile);
    EXPECT_EQ(tm.layers[0].get(0, 4), kEmptyTile);
    tm.layers[0].set(10, 10, 99);  // no-op, no crash
    EXPECT_EQ(tm.layers[0].get(0, 0), 1);
}

TEST(Tilemap, ResizePreservesOverlapZeroesNewCells)
{
    TilemapLayer layer;
    layer.resize(3, 3);
    for (int i = 0; i < 9; ++i)
    {
        layer.tiles[static_cast<std::size_t>(i)] = static_cast<TileId>(i + 1);
    }
    layer.resize(4, 2);
    // Overlap retained.
    EXPECT_EQ(layer.get(0, 0), 1);
    EXPECT_EQ(layer.get(2, 0), 3);
    EXPECT_EQ(layer.get(0, 1), 4);
    EXPECT_EQ(layer.get(2, 1), 6);
    // New column is empty.
    EXPECT_EQ(layer.get(3, 0), kEmptyTile);
    EXPECT_EQ(layer.get(3, 1), kEmptyTile);
}

TEST(Tilemap, ResolveStaticFrameName)
{
    auto tm = makeSampleTilemap();
    EXPECT_EQ(tm.resolveFrameName(1), "grass");
    EXPECT_EQ(tm.resolveFrameName(2), "stone");
}

TEST(Tilemap, ResolveEmptyIsBlank)
{
    auto tm = makeSampleTilemap();
    EXPECT_EQ(tm.resolveFrameName(kEmptyTile), "");
    EXPECT_EQ(tm.resolveFrameName(42), "");  // out-of-range tile id
}

TEST(Tilemap, ResolveAnimatedAdvancesWithTime)
{
    auto tm = makeSampleTilemap();
    // At t=0, animated tile shows its first frame.
    EXPECT_EQ(tm.resolveFrameName(3), "water_0");
    // Simulate 0.6s — past one frame period (0.5s), should wrap to frame 1.
    tm.update(0.6f);
    EXPECT_EQ(tm.resolveFrameName(3), "water_1");
    // Another 0.5s — back to frame 0 (forward-loop).
    tm.update(0.5f);
    EXPECT_EQ(tm.resolveFrameName(3), "water_0");
}

TEST(Tilemap, ForEachVisibleTileSkipsEmpty)
{
    auto tm = makeSampleTilemap(3, 3);
    tm.layers[0].set(0, 0, 1);
    tm.layers[0].set(2, 2, 2);

    int visited = 0;
    std::unordered_map<std::string, int> counts;
    tm.forEachVisibleTile([&](std::size_t, int, int, const std::string& frame) {
        ++visited;
        counts[frame]++;
        return true;
    });
    EXPECT_EQ(visited, 2);
    EXPECT_EQ(counts["grass"], 1);
    EXPECT_EQ(counts["stone"], 1);
}

TEST(Tilemap, ForEachVisibleStopsEarly)
{
    auto tm = makeSampleTilemap(3, 3);
    tm.layers[0].set(0, 0, 1);
    tm.layers[0].set(1, 0, 1);
    tm.layers[0].set(2, 0, 1);
    int visited = 0;
    tm.forEachVisibleTile([&](std::size_t, int, int, const std::string&) {
        ++visited;
        return visited < 2;  // stop after 2 visits
    });
    EXPECT_EQ(visited, 2);
}

TEST(Tilemap, CloneGetsZeroedAnimationTime)
{
    auto tm = makeSampleTilemap();
    tm.update(10.0f);
    ASSERT_GT(tm.animationElapsedSec, 0.0f);

    auto cloned = tm.clone();
    auto* tc = static_cast<TilemapComponent*>(cloned.get());
    EXPECT_FLOAT_EQ(tc->animationElapsedSec, 0.0f);
    EXPECT_EQ(tc->layers.size(), tm.layers.size());
    EXPECT_EQ(tc->tileDefs.size(), tm.tileDefs.size());
}

TEST(TilemapRenderer, BuildsInstancesForNonEmptyTiles)
{
    auto tm = makeSampleTilemap(3, 3);
    tm.tileWorldSize = 2.0f;
    tm.layers[0].set(0, 0, 1);
    tm.layers[0].set(1, 1, 2);

    std::vector<SpriteInstance> instances;
    auto worldMatrix = glm::mat4(1.0f);
    const auto* atlas = buildTilemapInstances(tm, worldMatrix, 0.995f, instances);

    ASSERT_NE(atlas, nullptr);
    ASSERT_EQ(instances.size(), 2u);

    // Instance 0: centred at (0 + 0.5) * 2 = 1, 1.
    EXPECT_FLOAT_EQ(instances[0].transformRow0.z, 1.0f);
    EXPECT_FLOAT_EQ(instances[0].transformRow1.z, 1.0f);
    EXPECT_FLOAT_EQ(instances[0].transformRow0.x, 2.0f);  // scale.x == tileSize
    EXPECT_FLOAT_EQ(instances[0].transformRow1.y, 2.0f);  // scale.y == tileSize

    // Instance 1: centred at (1 + 0.5) * 2 = 3, 3.
    EXPECT_FLOAT_EQ(instances[1].transformRow0.z, 3.0f);
    EXPECT_FLOAT_EQ(instances[1].transformRow1.z, 3.0f);

    // Depth and tint carried through.
    EXPECT_FLOAT_EQ(instances[0].depth, 0.995f);
    EXPECT_FLOAT_EQ(instances[0].tint.w, 1.0f);
}

TEST(TilemapRenderer, EmptyTilemapProducesNothing)
{
    auto tm = makeSampleTilemap(4, 4);
    std::vector<SpriteInstance> instances;
    auto worldMatrix = glm::mat4(1.0f);
    const auto* atlas = buildTilemapInstances(tm, worldMatrix, 0.995f, instances);
    EXPECT_EQ(atlas, nullptr);
    EXPECT_TRUE(instances.empty());
}
