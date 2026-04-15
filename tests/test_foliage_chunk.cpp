// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_foliage_chunk.cpp
/// @brief Unit tests for FoliageChunk and FoliageManager.
#include <gtest/gtest.h>

#include "environment/foliage_chunk.h"
#include "environment/foliage_manager.h"

using namespace Vestige;

// --- FoliageChunk tests ---

TEST(FoliageChunkTest, EmptyOnCreation)
{
    FoliageChunk chunk(0, 0);
    EXPECT_TRUE(chunk.isEmpty());
    EXPECT_EQ(chunk.getTotalInstanceCount(), 0);
}

TEST(FoliageChunkTest, AddFoliage)
{
    FoliageChunk chunk(0, 0);

    FoliageInstance inst;
    inst.position = glm::vec3(2.0f, 0.0f, 3.0f);
    inst.rotation = 1.0f;
    inst.scale = 1.0f;
    inst.colorTint = glm::vec3(1.0f);

    chunk.addFoliage(0, inst);
    EXPECT_FALSE(chunk.isEmpty());
    EXPECT_EQ(chunk.getTotalInstanceCount(), 1);
    EXPECT_EQ(chunk.getFoliage(0).size(), 1u);
}

TEST(FoliageChunkTest, AddMultipleTypes)
{
    FoliageChunk chunk(0, 0);

    FoliageInstance inst;
    inst.position = glm::vec3(1.0f, 0.0f, 1.0f);
    inst.scale = 1.0f;
    inst.colorTint = glm::vec3(1.0f);

    chunk.addFoliage(0, inst);
    chunk.addFoliage(0, inst);
    chunk.addFoliage(1, inst);

    EXPECT_EQ(chunk.getTotalInstanceCount(), 3);
    EXPECT_EQ(chunk.getFoliage(0).size(), 2u);
    EXPECT_EQ(chunk.getFoliage(1).size(), 1u);
    EXPECT_EQ(chunk.getFoliage(99).size(), 0u);  // Non-existent type
}

TEST(FoliageChunkTest, RemoveFoliageInRadius)
{
    FoliageChunk chunk(0, 0);

    // Add instances at known positions
    FoliageInstance near;
    near.position = glm::vec3(1.0f, 0.0f, 1.0f);
    near.scale = 1.0f;
    near.colorTint = glm::vec3(1.0f);

    FoliageInstance far;
    far.position = glm::vec3(10.0f, 0.0f, 10.0f);
    far.scale = 1.0f;
    far.colorTint = glm::vec3(1.0f);

    chunk.addFoliage(0, near);
    chunk.addFoliage(0, far);
    EXPECT_EQ(chunk.getTotalInstanceCount(), 2);

    // Remove within 3m of origin — should remove 'near' but not 'far'
    int removed = chunk.removeFoliageInRadius(0, glm::vec3(0.0f), 3.0f);
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(chunk.getFoliage(0).size(), 1u);
}

TEST(FoliageChunkTest, GetBounds)
{
    FoliageChunk chunk(1, 2);
    AABB bounds = chunk.getBounds();

    // Chunk at grid (1,2) => world X: [16, 32], Z: [32, 48]
    EXPECT_FLOAT_EQ(bounds.min.x, 16.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 32.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, 32.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, 48.0f);
}

TEST(FoliageChunkTest, NegativeGridCoords)
{
    FoliageChunk chunk(-1, -2);
    AABB bounds = chunk.getBounds();

    EXPECT_FLOAT_EQ(bounds.min.x, -16.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 0.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, -32.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, -16.0f);
}

TEST(FoliageChunkTest, ScatterAddRemove)
{
    FoliageChunk chunk(0, 0);

    ScatterInstance inst;
    inst.position = glm::vec3(2.0f, 0.0f, 2.0f);
    inst.scale = 1.0f;

    chunk.addScatter(inst);
    EXPECT_EQ(chunk.getScatter().size(), 1u);
    EXPECT_FALSE(chunk.isEmpty());

    int removed = chunk.removeScatterInRadius(glm::vec3(2.0f, 0.0f, 2.0f), 1.0f);
    EXPECT_EQ(removed, 1);
    EXPECT_TRUE(chunk.isEmpty());
}

TEST(FoliageChunkTest, TreeAddRemove)
{
    FoliageChunk chunk(0, 0);

    TreeInstance tree;
    tree.position = glm::vec3(5.0f, 0.0f, 5.0f);
    tree.scale = 1.0f;

    chunk.addTree(tree);
    EXPECT_EQ(chunk.getTrees().size(), 1u);

    // Remove with radius that doesn't reach
    int removed = chunk.removeTreesInRadius(glm::vec3(0.0f), 2.0f);
    EXPECT_EQ(removed, 0);
    EXPECT_EQ(chunk.getTrees().size(), 1u);

    // Remove with radius that does reach
    removed = chunk.removeTreesInRadius(glm::vec3(5.0f, 0.0f, 5.0f), 1.0f);
    EXPECT_EQ(removed, 1);
    EXPECT_TRUE(chunk.isEmpty());
}

TEST(FoliageChunkTest, SerializeDeserialize)
{
    FoliageChunk chunk(3, -1);

    FoliageInstance grass;
    grass.position = glm::vec3(50.0f, 0.0f, -10.0f);
    grass.rotation = 1.5f;
    grass.scale = 0.9f;
    grass.colorTint = glm::vec3(0.9f, 1.0f, 0.8f);
    chunk.addFoliage(0, grass);

    ScatterInstance rock;
    rock.position = glm::vec3(51.0f, 0.0f, -11.0f);
    rock.rotation = glm::quat(1, 0, 0, 0);
    rock.scale = 1.2f;
    rock.meshIndex = 2;
    chunk.addScatter(rock);

    TreeInstance tree;
    tree.position = glm::vec3(52.0f, 0.0f, -12.0f);
    tree.rotation = 2.0f;
    tree.scale = 1.5f;
    tree.speciesIndex = 1;
    chunk.addTree(tree);

    // Serialize
    nlohmann::json j = chunk.serialize();

    // Deserialize into a new chunk
    FoliageChunk loaded(3, -1);
    loaded.deserialize(j);

    // Verify foliage
    EXPECT_EQ(loaded.getFoliage(0).size(), 1u);
    const auto& loadedGrass = loaded.getFoliage(0)[0];
    EXPECT_NEAR(loadedGrass.position.x, 50.0f, 0.01f);
    EXPECT_NEAR(loadedGrass.rotation, 1.5f, 0.01f);
    EXPECT_NEAR(loadedGrass.scale, 0.9f, 0.01f);

    // Verify scatter
    EXPECT_EQ(loaded.getScatter().size(), 1u);
    EXPECT_EQ(loaded.getScatter()[0].meshIndex, 2u);

    // Verify trees
    EXPECT_EQ(loaded.getTrees().size(), 1u);
    EXPECT_EQ(loaded.getTrees()[0].speciesIndex, 1u);
    EXPECT_NEAR(loaded.getTrees()[0].scale, 1.5f, 0.01f);
}

// --- FoliageManager tests ---

TEST(FoliageManagerTest, PaintCreatesInstances)
{
    FoliageManager manager;

    FoliageTypeConfig config;
    config.minScale = 0.8f;
    config.maxScale = 1.2f;

    auto added = manager.paintFoliage(0, glm::vec3(0.0f), 5.0f, 2.0f, 0.3f, config);
    EXPECT_GT(added.size(), 0u);
    EXPECT_GT(manager.getTotalFoliageCount(), 0);
}

TEST(FoliageManagerTest, EraseRemovesInstances)
{
    FoliageManager manager;

    FoliageTypeConfig config;
    config.minScale = 0.8f;
    config.maxScale = 1.2f;

    manager.paintFoliage(0, glm::vec3(0.0f), 5.0f, 5.0f, 0.0f, config);
    int countBefore = manager.getTotalFoliageCount();
    EXPECT_GT(countBefore, 0);

    auto removed = manager.eraseFoliage(0, glm::vec3(0.0f), 3.0f);
    EXPECT_GT(removed.size(), 0u);
    EXPECT_LT(manager.getTotalFoliageCount(), countBefore);
}

TEST(FoliageManagerTest, RestoreAndRemoveForUndo)
{
    FoliageManager manager;

    FoliageTypeConfig config;
    config.minScale = 1.0f;
    config.maxScale = 1.0f;

    auto added = manager.paintFoliage(0, glm::vec3(0.0f), 3.0f, 2.0f, 0.0f, config);
    int countAfterPaint = manager.getTotalFoliageCount();

    // Undo the paint (remove the added instances)
    manager.removeFoliage(added);
    EXPECT_EQ(manager.getTotalFoliageCount(), 0);

    // Redo the paint (restore the removed instances)
    manager.restoreFoliage(added);
    EXPECT_EQ(manager.getTotalFoliageCount(), countAfterPaint);
}

TEST(FoliageManagerTest, PackUnpackChunkKey)
{
    int gx = -5;
    int gz = 42;
    uint64_t key = FoliageManager::packChunkKey(gx, gz);

    int outX, outZ;
    FoliageManager::unpackChunkKey(key, outX, outZ);
    EXPECT_EQ(outX, gx);
    EXPECT_EQ(outZ, gz);
}

TEST(FoliageManagerTest, SerializeDeserialize)
{
    FoliageManager manager;

    FoliageTypeConfig config;
    config.minScale = 0.8f;
    config.maxScale = 1.2f;

    manager.paintFoliage(0, glm::vec3(10.0f, 0.0f, 10.0f), 5.0f, 3.0f, 0.3f, config);
    manager.paintFoliage(1, glm::vec3(-10.0f, 0.0f, -10.0f), 5.0f, 3.0f, 0.3f, config);
    int originalCount = manager.getTotalFoliageCount();

    nlohmann::json j = manager.serialize();

    FoliageManager loaded;
    loaded.deserialize(j);

    EXPECT_EQ(loaded.getTotalFoliageCount(), originalCount);
}

TEST(FoliageManagerTest, ClearRemovesAll)
{
    FoliageManager manager;

    FoliageTypeConfig config;
    manager.paintFoliage(0, glm::vec3(0.0f), 5.0f, 3.0f, 0.0f, config);
    EXPECT_GT(manager.getTotalFoliageCount(), 0);

    manager.clear();
    EXPECT_EQ(manager.getTotalFoliageCount(), 0);
    EXPECT_EQ(manager.getChunkCount(), 0);
}
