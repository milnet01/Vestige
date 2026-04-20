// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sprite_renderer.cpp
/// @brief Unit tests for SpriteSystem's sort + batch logic (Phase 9F-1).
///
/// These tests drive the headless helpers (`sortDrawList`, `buildBatches`)
/// that the render path uses. GL-side tests are covered by the visual
/// smoke check in the workbench executable — here we validate the CPU
/// ordering, batching, and instance packing.
#include "renderer/sprite_atlas.h"
#include "scene/sprite_component.h"
#include "systems/sprite_system.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace Vestige;

namespace
{

std::shared_ptr<SpriteAtlas> makeAtlas(const std::string& tag)
{
    auto dir = std::filesystem::temp_directory_path() / "vestige_sprite_renderer_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / (std::string("atlas_") + tag + ".json");
    std::ofstream out(path);
    out << R"JSON({
      "frames": [
        { "filename": "main", "frame": {"x":0,"y":0,"w":32,"h":32},
          "sourceSize": {"w":32,"h":32} }
      ],
      "meta": { "image": ")JSON" << tag << R"JSON(.png", "size": {"w":32,"h":32} }
    })JSON";
    out.close();
    return SpriteAtlas::loadFromJson(path.string());
}

SpriteDrawEntry makeEntry(SpriteComponent& sc, uint32_t id,
                          int layer, int order, float y = 0.0f)
{
    sc.sortingLayer = layer;
    sc.orderInLayer = order;
    SpriteDrawEntry e;
    e.component = &sc;
    e.worldMatrix = glm::mat4(1.0f);
    e.worldMatrix[3].y = y;
    e.sortingLayer = layer;
    e.orderInLayer = order;
    e.yForSort = y;
    e.entityId = id;
    return e;
}

} // namespace

TEST(SpriteSystem, SortByLayerThenOrder)
{
    auto atlas = makeAtlas("a");
    ASSERT_TRUE(atlas);

    SpriteComponent c0; c0.atlas = atlas; c0.frameName = "main";
    SpriteComponent c1; c1.atlas = atlas; c1.frameName = "main";
    SpriteComponent c2; c2.atlas = atlas; c2.frameName = "main";

    std::vector<SpriteDrawEntry> entries = {
        makeEntry(c2, 3, /*layer*/ 5, /*order*/ 1),
        makeEntry(c0, 1, /*layer*/ 0, /*order*/ 2),
        makeEntry(c1, 2, /*layer*/ 5, /*order*/ 0),
    };

    SpriteSystem::sortDrawList(entries);
    EXPECT_EQ(entries[0].entityId, 1u);  // layer 0
    EXPECT_EQ(entries[1].entityId, 2u);  // layer 5, order 0
    EXPECT_EQ(entries[2].entityId, 3u);  // layer 5, order 1
}

TEST(SpriteSystem, SortByYWhenOptedIn)
{
    auto atlas = makeAtlas("y");
    ASSERT_TRUE(atlas);

    SpriteComponent front;  front.atlas = atlas; front.frameName = "main"; front.sortByY = true;
    SpriteComponent back;   back.atlas  = atlas; back.frameName  = "main"; back.sortByY  = true;

    // Same layer + order; larger y draws first (appears further back).
    std::vector<SpriteDrawEntry> entries = {
        makeEntry(front, 1, 0, 0, /*y*/ -2.0f),  // closer to camera — draws last
        makeEntry(back,  2, 0, 0, /*y*/  3.0f),  // further away — draws first
    };
    SpriteSystem::sortDrawList(entries);
    EXPECT_EQ(entries[0].entityId, 2u);
    EXPECT_EQ(entries[1].entityId, 1u);
}

TEST(SpriteSystem, SortByYIgnoredWhenFlagOff)
{
    auto atlas = makeAtlas("y2");
    ASSERT_TRUE(atlas);

    // sortByY defaults to false on both.
    SpriteComponent a;  a.atlas = atlas;  a.frameName = "main";
    SpriteComponent b;  b.atlas = atlas;  b.frameName = "main";

    std::vector<SpriteDrawEntry> entries = {
        makeEntry(a, 10, 0, 0, /*y*/ 3.0f),
        makeEntry(b, 11, 0, 0, /*y*/ -2.0f),
    };
    SpriteSystem::sortDrawList(entries);
    // Fallback to entityId: 10 < 11.
    EXPECT_EQ(entries[0].entityId, 10u);
    EXPECT_EQ(entries[1].entityId, 11u);
}

TEST(SpriteSystem, BatchesByAtlasAndPass)
{
    auto atlasA = makeAtlas("ba");
    auto atlasB = makeAtlas("bb");
    ASSERT_TRUE(atlasA);
    ASSERT_TRUE(atlasB);

    SpriteComponent opaqueA;    opaqueA.atlas = atlasA;  opaqueA.frameName = "main";
    opaqueA.isTransparent = false;
    SpriteComponent transA;     transA.atlas  = atlasA;  transA.frameName  = "main";
    transA.isTransparent = true;
    SpriteComponent transB;     transB.atlas  = atlasB;  transB.frameName  = "main";
    transB.isTransparent = true;

    std::vector<SpriteDrawEntry> entries = {
        makeEntry(opaqueA, 1, 0, 0),
        makeEntry(transA,  2, 0, 1),
        makeEntry(transA,  3, 0, 2),
        makeEntry(transB,  4, 0, 3),
    };
    SpriteSystem::sortDrawList(entries);
    auto batches = SpriteSystem::buildBatches(entries);
    ASSERT_EQ(batches.size(), 3u);
    EXPECT_EQ(batches[0].pass, SpritePass::Opaque);
    EXPECT_EQ(batches[0].atlas, atlasA.get());
    EXPECT_EQ(batches[0].instances.size(), 1u);
    EXPECT_EQ(batches[1].pass, SpritePass::Transparent);
    EXPECT_EQ(batches[1].atlas, atlasA.get());
    EXPECT_EQ(batches[1].instances.size(), 2u);
    EXPECT_EQ(batches[2].pass, SpritePass::Transparent);
    EXPECT_EQ(batches[2].atlas, atlasB.get());
    EXPECT_EQ(batches[2].instances.size(), 1u);
}

TEST(SpriteSystem, InstancePackingWritesUvAndTint)
{
    auto atlas = makeAtlas("pack");
    ASSERT_TRUE(atlas);

    SpriteComponent sc;
    sc.atlas = atlas;
    sc.frameName = "main";
    sc.tint = glm::vec4(0.5f, 0.25f, 0.75f, 1.0f);

    std::vector<SpriteDrawEntry> entries = { makeEntry(sc, 7, 0, 0) };
    auto batches = SpriteSystem::buildBatches(entries);
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].instances.size(), 1u);
    const auto& inst = batches[0].instances[0];

    // UV rect comes from atlas frame (0..1 range for a 32x32 frame in a 32x32 atlas).
    EXPECT_FLOAT_EQ(inst.uvRect.x, 0.0f);
    EXPECT_FLOAT_EQ(inst.uvRect.y, 0.0f);
    EXPECT_FLOAT_EQ(inst.uvRect.z, 1.0f);
    EXPECT_FLOAT_EQ(inst.uvRect.w, 1.0f);

    EXPECT_FLOAT_EQ(inst.tint.x, 0.5f);
    EXPECT_FLOAT_EQ(inst.tint.y, 0.25f);
    EXPECT_FLOAT_EQ(inst.tint.z, 0.75f);
    EXPECT_FLOAT_EQ(inst.tint.w, 1.0f);
}

TEST(SpriteSystem, InstanceDepthMonotonic)
{
    auto atlas = makeAtlas("depth");
    ASSERT_TRUE(atlas);

    // Five sprites on distinct orderInLayer — after sort, depth should be
    // strictly non-decreasing.
    std::vector<SpriteComponent> comps(5);
    for (auto& c : comps) { c.atlas = atlas; c.frameName = "main"; }

    std::vector<SpriteDrawEntry> entries;
    for (int i = 0; i < 5; ++i)
    {
        entries.push_back(makeEntry(comps[static_cast<std::size_t>(i)],
                                    static_cast<uint32_t>(100 + i),
                                    /*layer*/ 0, /*order*/ i));
    }
    SpriteSystem::sortDrawList(entries);
    auto batches = SpriteSystem::buildBatches(entries);
    ASSERT_EQ(batches.size(), 1u);
    const auto& batch = batches[0];
    ASSERT_EQ(batch.instances.size(), 5u);
    for (std::size_t i = 1; i < batch.instances.size(); ++i)
    {
        EXPECT_GE(batch.instances[i].depth, batch.instances[i - 1].depth);
    }
    // All within the sprite depth band documented in the shader.
    EXPECT_GE(batch.instances.front().depth, 0.98f);
    EXPECT_LE(batch.instances.back().depth, 1.0f);
}

TEST(SpriteSystem, MissingFrameIsSkippedNotFailed)
{
    auto atlas = makeAtlas("missing");
    ASSERT_TRUE(atlas);

    SpriteComponent valid;   valid.atlas   = atlas; valid.frameName   = "main";
    SpriteComponent stale;   stale.atlas   = atlas; stale.frameName   = "renamed_in_asset";

    std::vector<SpriteDrawEntry> entries = {
        makeEntry(valid, 1, 0, 0),
        makeEntry(stale, 2, 0, 1),
    };
    auto batches = SpriteSystem::buildBatches(entries);
    ASSERT_EQ(batches.size(), 1u);
    // Stale frame dropped, valid one survives.
    EXPECT_EQ(batches[0].instances.size(), 1u);
}

TEST(SpriteSystem, ClonedComponentHasIndependentAnimation)
{
    auto atlas = makeAtlas("clone");
    ASSERT_TRUE(atlas);

    SpriteComponent original;
    original.atlas = atlas;
    original.frameName = "main";
    original.animation = std::make_shared<SpriteAnimation>();
    SpriteAnimationClip clip;
    clip.name = "idle";
    clip.frames = {{"main", 50.0f}, {"main", 50.0f}};
    original.animation->addClip(clip);
    original.animation->play("idle");

    auto cloned = original.clone();
    ASSERT_TRUE(cloned);
    auto* sc = static_cast<SpriteComponent*>(cloned.get());
    // The animation pointer is cloned, not shared — advancing one should
    // not advance the other.
    ASSERT_TRUE(sc->animation);
    EXPECT_NE(sc->animation.get(), original.animation.get());

    original.animation->tick(0.060f);
    EXPECT_EQ(original.animation->currentFrameIndex(), 1);
    EXPECT_EQ(sc->animation->currentFrameIndex(), 0);
}
