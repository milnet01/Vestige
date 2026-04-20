// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sprite_panel.cpp
/// @brief Headless tests for SpritePanel + TilemapPanel state (Phase 9F-6).
///
/// The panels' ImGui-dependent draw() is not exercised here — these tests
/// drive the public logic that doesn't need a live ImGui context
/// (atlas loading, paint/erase operations, visibility toggles).
#include "editor/panels/sprite_panel.h"
#include "editor/panels/tilemap_panel.h"
#include "renderer/sprite_atlas.h"
#include "scene/tilemap_component.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace Vestige;

namespace
{

std::string writeAtlas()
{
    auto dir = std::filesystem::temp_directory_path() / "vestige_sprite_panel_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "atlas.json";
    std::ofstream out(path);
    out << R"JSON({
      "frames": [
        {"filename":"idle","frame":{"x":0,"y":0,"w":16,"h":16},
         "sourceSize":{"w":16,"h":16}}
      ],
      "meta": {"image":"test.png","size":{"w":16,"h":16}}
    })JSON";
    return path.string();
}

} // namespace

TEST(SpritePanel, VisibilityToggles)
{
    SpritePanel p;
    EXPECT_FALSE(p.isVisible());
    p.toggleVisible();
    EXPECT_TRUE(p.isVisible());
    p.setVisible(false);
    EXPECT_FALSE(p.isVisible());
}

TEST(SpritePanel, LoadsAtlasFromDisk)
{
    SpritePanel p;
    EXPECT_TRUE(p.loadAtlasFromPath(writeAtlas()));
    auto atlas = p.getLoadedAtlas();
    ASSERT_NE(atlas, nullptr);
    EXPECT_EQ(atlas->frameCount(), 1u);
    EXPECT_EQ(atlas->imageName(), "test.png");
}

TEST(SpritePanel, LoadFailsGracefully)
{
    SpritePanel p;
    EXPECT_FALSE(p.loadAtlasFromPath("/tmp/__does_not_exist__.json"));
    EXPECT_EQ(p.getLoadedAtlas(), nullptr);
}

TEST(TilemapPanel, PaintAndEraseCells)
{
    TilemapComponent tm;
    tm.tileDefs.resize(3);
    tm.tileDefs[1].atlasFrameName = "grass";
    tm.tileDefs[2].atlasFrameName = "stone";
    tm.addLayer("base", 4, 4);

    TilemapPanel p;
    p.setActiveLayer(0);
    p.setActiveTileId(2);

    EXPECT_TRUE(p.paintCell(tm, 1, 2));
    EXPECT_EQ(tm.layers[0].get(1, 2), 2);

    p.setActiveTileId(1);
    EXPECT_TRUE(p.paintCell(tm, 0, 0));
    EXPECT_EQ(tm.layers[0].get(0, 0), 1);

    EXPECT_TRUE(p.eraseCell(tm, 1, 2));
    EXPECT_EQ(tm.layers[0].get(1, 2), 0);
}

TEST(TilemapPanel, PaintIgnoresInvalidActiveLayer)
{
    TilemapComponent tm;
    tm.addLayer("l", 2, 2);
    TilemapPanel p;
    p.setActiveLayer(5);  // out of range
    EXPECT_FALSE(p.paintCell(tm, 0, 0));
    EXPECT_EQ(tm.layers[0].get(0, 0), 0);
}

TEST(TilemapPanel, VisibilityToggles)
{
    TilemapPanel p;
    EXPECT_FALSE(p.isVisible());
    p.toggleVisible();
    EXPECT_TRUE(p.isVisible());
    p.setVisible(false);
    EXPECT_FALSE(p.isVisible());
}
