// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_instanced_rendering.cpp
/// @brief Unit tests for instanced rendering batch-building logic.
#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include "renderer/renderer.h"
#include "scene/scene.h"

using namespace Vestige;

// Helper: create a RenderItem with dummy pointer values (never dereferenced during batching)
static SceneRenderData::RenderItem makeItem(uintptr_t meshId, uintptr_t matId,
                                             const glm::mat4& matrix = glm::mat4(1.0f))
{
    SceneRenderData::RenderItem item;
    item.mesh = reinterpret_cast<const Mesh*>(meshId);
    item.material = reinterpret_cast<const Material*>(matId);
    item.worldMatrix = matrix;
    item.worldBounds = {};
    return item;
}

// =============================================================================
// Batch building tests
// =============================================================================

TEST(InstanceBatchTest, EmptyItemsProducesNoBatches)
{
    std::vector<SceneRenderData::RenderItem> items;
    auto batches = Renderer::buildInstanceBatchesStatic(items);
    EXPECT_TRUE(batches.empty());
}

TEST(InstanceBatchTest, SingleItemProducesSingleBatch)
{
    std::vector<SceneRenderData::RenderItem> items = {makeItem(1, 1)};
    auto batches = Renderer::buildInstanceBatchesStatic(items);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].modelMatrices.size(), 1u);
}

TEST(InstanceBatchTest, SameMeshMaterialGroupsTogether)
{
    glm::mat4 m1 = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 m2 = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
    glm::mat4 m3 = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));

    std::vector<SceneRenderData::RenderItem> items = {
        makeItem(1, 1, m1),
        makeItem(1, 1, m2),
        makeItem(1, 1, m3),
    };

    auto batches = Renderer::buildInstanceBatchesStatic(items);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].modelMatrices.size(), 3u);
}

TEST(InstanceBatchTest, DifferentMeshesSeparateBatches)
{
    std::vector<SceneRenderData::RenderItem> items = {
        makeItem(1, 1),
        makeItem(2, 1),  // Different mesh, same material
    };

    auto batches = Renderer::buildInstanceBatchesStatic(items);
    EXPECT_EQ(batches.size(), 2u);
}

TEST(InstanceBatchTest, DifferentMaterialsSeparateBatches)
{
    std::vector<SceneRenderData::RenderItem> items = {
        makeItem(1, 1),
        makeItem(1, 2),  // Same mesh, different material
    };

    auto batches = Renderer::buildInstanceBatchesStatic(items);
    EXPECT_EQ(batches.size(), 2u);
}

TEST(InstanceBatchTest, BatchPreservesAllMatrices)
{
    glm::mat4 m1 = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 m2 = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));

    std::vector<SceneRenderData::RenderItem> items = {
        makeItem(1, 1, m1),
        makeItem(1, 1, m2),
    };

    auto batches = Renderer::buildInstanceBatchesStatic(items);
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].modelMatrices.size(), 2u);

    // Verify matrices are preserved (column 3 = translation)
    EXPECT_FLOAT_EQ(batches[0].modelMatrices[0][3][0], 1.0f);
    EXPECT_FLOAT_EQ(batches[0].modelMatrices[1][3][1], 2.0f);
}

TEST(InstanceBatchTest, MixedItemsProduceCorrectBatchCount)
{
    // 3 unique (mesh, material) pairs
    std::vector<SceneRenderData::RenderItem> items = {
        makeItem(1, 1),
        makeItem(2, 2),
        makeItem(1, 1),  // Groups with first
        makeItem(3, 3),
        makeItem(2, 2),  // Groups with second
        makeItem(1, 1),  // Groups with first
    };

    auto batches = Renderer::buildInstanceBatchesStatic(items);
    EXPECT_EQ(batches.size(), 3u);

    // Find batch for mesh=1, mat=1
    size_t totalInstances = 0;
    for (const auto& batch : batches)
    {
        totalInstances += batch.modelMatrices.size();
    }
    EXPECT_EQ(totalInstances, 6u);
}

// =============================================================================
// Threshold tests
// =============================================================================

// These two tests pin the batch size against MIN_INSTANCE_BATCH_SIZE — the
// constant the draw loop consults to choose the instanced vs non-instanced
// path (renderer.cpp: `count >= MIN_INSTANCE_BATCH_SIZE`). The path itself
// needs a GL context to exercise, so the closest test-only proxy is the
// size-vs-threshold comparison against the live constant: if either the
// batch size or the threshold value drifts, these fail. (Asserting against a
// bare literal `2` would silently pass if the constant changed.)
TEST(InstanceBatchTest, SingleInstanceBelowThreshold)
{
    std::vector<SceneRenderData::RenderItem> items = {makeItem(1, 1)};
    auto batches = Renderer::buildInstanceBatchesStatic(items);
    ASSERT_EQ(batches.size(), 1u);
    const int count = static_cast<int>(batches[0].modelMatrices.size());
    EXPECT_LT(count, Renderer::MIN_INSTANCE_BATCH_SIZE);
}

TEST(InstanceBatchTest, TwoInstancesMeetsThreshold)
{
    std::vector<SceneRenderData::RenderItem> items = {
        makeItem(1, 1),
        makeItem(1, 1),
    };
    auto batches = Renderer::buildInstanceBatchesStatic(items);
    ASSERT_EQ(batches.size(), 1u);
    const int count = static_cast<int>(batches[0].modelMatrices.size());
    EXPECT_GE(count, Renderer::MIN_INSTANCE_BATCH_SIZE);
}

// =============================================================================
// Hash correctness
// =============================================================================

TEST(InstanceBatchTest, SamePointersProduceSameBatch)
{
    // Same mesh+material should always end up in one batch
    std::vector<SceneRenderData::RenderItem> items;
    for (int i = 0; i < 100; i++)
    {
        items.push_back(makeItem(42, 99));
    }

    auto batches = Renderer::buildInstanceBatchesStatic(items);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].modelMatrices.size(), 100u);
}

TEST(InstanceBatchTest, ManyUniquePairsAllSeparate)
{
    std::vector<SceneRenderData::RenderItem> items;
    for (uintptr_t i = 1; i <= 50; i++)
    {
        items.push_back(makeItem(i, i));
    }

    auto batches = Renderer::buildInstanceBatchesStatic(items);
    EXPECT_EQ(batches.size(), 50u);
}

// =============================================================================
// Slice R2 — skinned/morphed items must never coalesce into an instanced batch
// (the instanced path forces u_hasBones=false ⇒ bind pose). §9.3 routing fix.
// =============================================================================

TEST(InstanceBatchTest, SkinnedMeshNeverInstanceBatched_R2)
{
    // Two items with the SAME mesh+material pointer, both skinned (non-null
    // boneMatrices). Under R1 they would group into one instanced batch and render at
    // bind pose; R2 must keep them as two single-instance batches.
    static const std::vector<glm::mat4> palette(4, glm::mat4(1.0f));

    auto a = makeItem(1, 1);
    auto b = makeItem(1, 1, glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0)));
    a.boneMatrices = &palette;
    b.boneMatrices = &palette;

    std::vector<SceneRenderData::RenderItem> items = {a, b};
    auto batches = Renderer::buildInstanceBatchesStatic(items);

    ASSERT_EQ(batches.size(), 2u) << "shared-mesh skinned items must not merge";
    for (const auto& batch : batches)
    {
        // Each is a single-instance batch → below the instancing threshold → routed
        // through the per-item skinning draw with u_hasBones=true.
        EXPECT_EQ(batch.modelMatrices.size(), 1u);
        EXPECT_LT(static_cast<int>(batch.modelMatrices.size()),
                  Renderer::MIN_INSTANCE_BATCH_SIZE);
        ASSERT_EQ(batch.boneMatrixPtrs.size(), 1u);
        EXPECT_NE(batch.boneMatrixPtrs[0], nullptr);
    }
}

TEST(InstanceBatchTest, MorphedMeshNeverInstanceBatched_R2)
{
    // Same as above but driven by morph weights instead of bones.
    static const std::vector<float> weights = {1.0f, 0.0f};

    auto a = makeItem(2, 2);
    auto b = makeItem(2, 2, glm::translate(glm::mat4(1.0f), glm::vec3(3, 0, 0)));
    a.morphWeights = &weights;
    b.morphWeights = &weights;

    std::vector<SceneRenderData::RenderItem> items = {a, b};
    auto batches = Renderer::buildInstanceBatchesStatic(items);

    ASSERT_EQ(batches.size(), 2u) << "shared-mesh morphed items must not merge";
    for (const auto& batch : batches)
    {
        EXPECT_EQ(batch.modelMatrices.size(), 1u);
    }
}

TEST(InstanceBatchTest, StaticMeshesStillGroupAfterRoutingFix_R2)
{
    // Regression guard: non-skinned, non-morphed shared-mesh items still coalesce
    // (the routing fix must not break ordinary instancing).
    std::vector<SceneRenderData::RenderItem> items = {makeItem(3, 3), makeItem(3, 3)};
    auto batches = Renderer::buildInstanceBatchesStatic(items);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].modelMatrices.size(), 2u);
}
