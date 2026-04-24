// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sprite_atlas.cpp
/// @brief Unit tests for SpriteAtlas JSON-Array loader (Phase 9F-1).
#include "renderer/sprite_atlas.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace Vestige;

namespace
{

/// @brief Writes @p content to a unique temporary file and returns the path.
/// The file lives for the lifetime of the test binary — fine for the test
/// count here. GoogleTest doesn't require cleanup.
std::string writeTempFile(const std::string& suffix, const std::string& content)
{
    // Per-test scratch dir so parallel ctest processes don't collide.
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string key = info ? info->name() : "unknown";
    auto dir = std::filesystem::temp_directory_path() / ("vestige_sprite_atlas_test_" + key);
    std::filesystem::create_directories(dir);
    static int counter = 0;
    auto path = dir / (std::string("atlas_") + std::to_string(counter++) + "_" + suffix);
    std::ofstream out(path);
    out << content;
    return path.string();
}

constexpr const char* kValidAtlas = R"JSON({
  "frames": [
    { "filename": "idle_0", "frame": {"x": 0,  "y": 0,  "w": 32, "h": 32},
      "sourceSize": {"w": 32, "h": 32} },
    { "filename": "idle_1", "frame": {"x": 32, "y": 0,  "w": 32, "h": 32},
      "sourceSize": {"w": 32, "h": 32} },
    { "filename": "run_0",  "frame": {"x": 0,  "y": 32, "w": 64, "h": 32},
      "sourceSize": {"w": 64, "h": 32},
      "pivot": {"x": 0.5, "y": 1.0} }
  ],
  "meta": {
    "image": "char.png",
    "size": {"w": 128, "h": 128}
  }
})JSON";

constexpr const char* kHashShapeAtlas = R"JSON({
  "frames": {
    "alpha": { "frame": {"x": 0, "y": 0, "w": 16, "h": 16} },
    "beta":  { "frame": {"x": 16,"y": 0, "w": 16, "h": 16} }
  },
  "meta": {
    "image": "hash.png",
    "size": {"w": 32, "h": 16}
  }
})JSON";

} // namespace

TEST(SpriteAtlas, LoadsArrayForm)
{
    const auto path = writeTempFile("array.json", kValidAtlas);
    auto atlas = SpriteAtlas::loadFromJson(path);
    ASSERT_TRUE(atlas);
    EXPECT_EQ(atlas->frameCount(), 3u);
    EXPECT_EQ(atlas->imageName(), "char.png");
    EXPECT_FLOAT_EQ(atlas->atlasSize().x, 128.0f);
    EXPECT_FLOAT_EQ(atlas->atlasSize().y, 128.0f);
}

TEST(SpriteAtlas, LookupByName)
{
    const auto path = writeTempFile("lookup.json", kValidAtlas);
    auto atlas = SpriteAtlas::loadFromJson(path);
    ASSERT_TRUE(atlas);

    const auto* idle1 = atlas->find("idle_1");
    ASSERT_NE(idle1, nullptr);
    EXPECT_FLOAT_EQ(idle1->uv.x, 32.0f / 128.0f);
    EXPECT_FLOAT_EQ(idle1->uv.y, 0.0f);
    EXPECT_FLOAT_EQ(idle1->uv.z, 64.0f / 128.0f);
    EXPECT_FLOAT_EQ(idle1->uv.w, 32.0f / 128.0f);

    EXPECT_EQ(atlas->find("does_not_exist"), nullptr);
}

TEST(SpriteAtlas, PivotDefaultsToCenter)
{
    const auto path = writeTempFile("pivot.json", kValidAtlas);
    auto atlas = SpriteAtlas::loadFromJson(path);
    ASSERT_TRUE(atlas);
    const auto* idle = atlas->find("idle_0");
    ASSERT_NE(idle, nullptr);
    EXPECT_FLOAT_EQ(idle->pivot.x, 0.5f);
    EXPECT_FLOAT_EQ(idle->pivot.y, 0.5f);

    const auto* run = atlas->find("run_0");
    ASSERT_NE(run, nullptr);
    EXPECT_FLOAT_EQ(run->pivot.x, 0.5f);
    EXPECT_FLOAT_EQ(run->pivot.y, 1.0f);
}

TEST(SpriteAtlas, LoadsHashForm)
{
    const auto path = writeTempFile("hash.json", kHashShapeAtlas);
    auto atlas = SpriteAtlas::loadFromJson(path);
    ASSERT_TRUE(atlas);
    EXPECT_EQ(atlas->frameCount(), 2u);
    // Hash order is not guaranteed; lookup is.
    EXPECT_NE(atlas->find("alpha"), nullptr);
    EXPECT_NE(atlas->find("beta"), nullptr);
}

TEST(SpriteAtlas, FramesReturnDeclarationOrder)
{
    const auto path = writeTempFile("order.json", kValidAtlas);
    auto atlas = SpriteAtlas::loadFromJson(path);
    ASSERT_TRUE(atlas);
    const auto names = atlas->frameNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "idle_0");
    EXPECT_EQ(names[1], "idle_1");
    EXPECT_EQ(names[2], "run_0");
}

TEST(SpriteAtlas, MissingFileReturnsNull)
{
    auto atlas = SpriteAtlas::loadFromJson("/tmp/does_not_exist_atlas.json");
    EXPECT_FALSE(atlas);
}

TEST(SpriteAtlas, MalformedJsonReturnsNull)
{
    const auto path = writeTempFile("bad.json", R"({ "frames": [ not valid )");
    auto atlas = SpriteAtlas::loadFromJson(path);
    EXPECT_FALSE(atlas);
}

TEST(SpriteAtlas, MissingMetaSizeReturnsNull)
{
    const auto path = writeTempFile("no_size.json", R"JSON({
      "frames": [{"filename":"x","frame":{"x":0,"y":0,"w":8,"h":8}}],
      "meta": {"image": "x.png"}
    })JSON");
    auto atlas = SpriteAtlas::loadFromJson(path);
    EXPECT_FALSE(atlas);
}

TEST(SpriteAtlas, TextureIdPersists)
{
    const auto path = writeTempFile("tex.json", kValidAtlas);
    auto atlas = SpriteAtlas::loadFromJson(path);
    ASSERT_TRUE(atlas);
    EXPECT_EQ(atlas->textureId(), 0u);
    atlas->setTextureId(42);
    EXPECT_EQ(atlas->textureId(), 42u);
}
