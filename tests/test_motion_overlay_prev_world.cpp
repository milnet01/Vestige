// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_motion_overlay_prev_world.cpp
/// @brief Phase 10.9 Slice 4 R10 — motion-overlay prev-world cache contract.
///
/// Contract (authored from ROADMAP Phase 10.9 Slice 4 R10 and the
/// `engine/renderer/motion_overlay_prev_world.h` design):
///
///   `Renderer::renderScene` updates a per-entity prev-frame world
///   matrix cache (`m_prevWorldMatrices`) at end-of-frame so the
///   per-object motion-vector overlay can sample prev→curr per
///   entity on the next frame. Before R10 the clear + populate
///   block was inside `if (isTaa)` only, so non-TAA modes (MSAA /
///   SMAA / None) carried stale entries from a prior TAA session
///   forever — the motion overlay then read those stale matrices
///   on a subsequent TAA toggle-back, blending current geometry
///   against a possibly-different mesh's old transform (entityIds
///   get reused over time).
///
///   `updateMotionOverlayPrevWorld(cache, isTaa, renderItems,
///   transparentItems)` enforces:
///     - `cache.clear()` is unconditional (closes the cross-mode-
///       switch staleness window).
///     - Population runs only when `isTaa` is true (the cache is
///       read only on the TAA path; non-TAA modes need only the
///       clear).
///     - Items with `entityId == 0` are skipped.

#include <gtest/gtest.h>

#include "renderer/motion_overlay_prev_world.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace
{

struct MockRenderItem
{
    uint32_t entityId = 0;
    glm::mat4 worldMatrix = glm::mat4(1.0f);
};

glm::mat4 makeOffsetMat(float tx, float ty, float tz)
{
    glm::mat4 m(1.0f);
    m[3][0] = tx;
    m[3][1] = ty;
    m[3][2] = tz;
    return m;
}

} // namespace

using namespace Vestige;

TEST(MotionOverlayPrevWorld, NonTaaModeClearsCache_R10)
{
    std::unordered_map<uint32_t, glm::mat4> cache;
    cache[42] = makeOffsetMat(1.0f, 2.0f, 3.0f);
    cache[7] = makeOffsetMat(0.5f, 0.0f, 0.0f);

    std::vector<MockRenderItem> empty;
    updateMotionOverlayPrevWorld(cache, /*isTaa=*/false, empty, empty);

    // Headline R10 invariant — non-TAA mode wipes stale entries the
    // motion overlay would otherwise read on the next TAA toggle-back.
    EXPECT_TRUE(cache.empty()) << "non-TAA mode must clear the cache";
}

TEST(MotionOverlayPrevWorld, NonTaaModeWithItemsStillClears_R10)
{
    std::unordered_map<uint32_t, glm::mat4> cache;
    cache[100] = makeOffsetMat(9.0f, 9.0f, 9.0f);  // pre-populated stale

    std::vector<MockRenderItem> items {
        {1, makeOffsetMat(1, 0, 0)},
        {2, makeOffsetMat(0, 1, 0)},
    };
    std::vector<MockRenderItem> empty;

    updateMotionOverlayPrevWorld(cache, /*isTaa=*/false, items, empty);

    // Even when the current frame has items to track, non-TAA mode
    // must still clear (no consumer of the cache exists outside TAA).
    EXPECT_TRUE(cache.empty());
}

TEST(MotionOverlayPrevWorld, TaaModeClearsAndPopulatesFromCurrent_R10)
{
    std::unordered_map<uint32_t, glm::mat4> cache;
    cache[999] = makeOffsetMat(50, 50, 50);  // stale entry from prior frame

    std::vector<MockRenderItem> items {
        {1, makeOffsetMat(1, 0, 0)},
        {2, makeOffsetMat(0, 1, 0)},
    };
    std::vector<MockRenderItem> transparents;

    updateMotionOverlayPrevWorld(cache, /*isTaa=*/true, items, transparents);

    // Stale entry gone; current entries present.
    EXPECT_EQ(cache.size(), 2u);
    ASSERT_TRUE(cache.count(1));
    ASSERT_TRUE(cache.count(2));
    EXPECT_FALSE(cache.count(999)) << "stale entry must not survive the clear";
    EXPECT_EQ(cache.at(1)[3][0], 1.0f);
    EXPECT_EQ(cache.at(2)[3][1], 1.0f);
}

TEST(MotionOverlayPrevWorld, TaaModeIncludesTransparentItems_R10)
{
    std::unordered_map<uint32_t, glm::mat4> cache;

    std::vector<MockRenderItem> opaque {
        {10, makeOffsetMat(1, 0, 0)},
    };
    std::vector<MockRenderItem> transparents {
        {20, makeOffsetMat(0, 0, 1)},
    };

    updateMotionOverlayPrevWorld(cache, /*isTaa=*/true, opaque, transparents);

    EXPECT_EQ(cache.size(), 2u);
    EXPECT_TRUE(cache.count(10));
    EXPECT_TRUE(cache.count(20));
}

TEST(MotionOverlayPrevWorld, EntityIdZeroIsSkipped_R10)
{
    std::unordered_map<uint32_t, glm::mat4> cache;

    std::vector<MockRenderItem> items {
        {0, makeOffsetMat(99, 99, 99)},  // sentinel — skipped
        {5, makeOffsetMat(1, 0, 0)},
        {0, makeOffsetMat(0, 0, 0)},  // also skipped
    };
    std::vector<MockRenderItem> empty;

    updateMotionOverlayPrevWorld(cache, /*isTaa=*/true, items, empty);

    EXPECT_EQ(cache.size(), 1u);
    EXPECT_TRUE(cache.count(5));
    EXPECT_FALSE(cache.count(0));
}

TEST(MotionOverlayPrevWorld, EmptyRenderDataIsTaaClearsCache_R10)
{
    std::unordered_map<uint32_t, glm::mat4> cache;
    cache[1] = makeOffsetMat(1, 0, 0);
    cache[2] = makeOffsetMat(0, 1, 0);

    std::vector<MockRenderItem> empty;
    updateMotionOverlayPrevWorld(cache, /*isTaa=*/true, empty, empty);

    // Even TAA mode with no items must clear; the previous frame's
    // entries are no longer "current" and the overlay wouldn't read
    // them sensibly (entityIds may have been freed).
    EXPECT_TRUE(cache.empty());
}

TEST(MotionOverlayPrevWorld, RepeatedCallsConvergeOnLatestFrame_R10)
{
    std::unordered_map<uint32_t, glm::mat4> cache;

    std::vector<MockRenderItem> frameA { {1, makeOffsetMat(1, 0, 0)} };
    std::vector<MockRenderItem> frameB { {1, makeOffsetMat(2, 0, 0)} };
    std::vector<MockRenderItem> empty;

    updateMotionOverlayPrevWorld(cache, /*isTaa=*/true, frameA, empty);
    EXPECT_EQ(cache.at(1)[3][0], 1.0f);

    updateMotionOverlayPrevWorld(cache, /*isTaa=*/true, frameB, empty);
    EXPECT_EQ(cache.at(1)[3][0], 2.0f) << "subsequent frame must overwrite";
}

TEST(MotionOverlayPrevWorld, ModeSwitchTaaToNonTaaWipesCache_R10)
{
    // The exact bug R10 fixes: TAA populates → user toggles to MSAA →
    // cache must be cleared so a future toggle-back to TAA doesn't
    // read matrices that may belong to entities that have since been
    // destroyed (and their entityIds reused).
    std::unordered_map<uint32_t, glm::mat4> cache;

    std::vector<MockRenderItem> taaFrame {
        {42, makeOffsetMat(7, 7, 7)},
        {99, makeOffsetMat(8, 8, 8)},
    };
    std::vector<MockRenderItem> empty;

    updateMotionOverlayPrevWorld(cache, /*isTaa=*/true, taaFrame, empty);
    ASSERT_EQ(cache.size(), 2u);

    // Switch off TAA next frame.
    updateMotionOverlayPrevWorld(cache, /*isTaa=*/false, empty, empty);
    EXPECT_TRUE(cache.empty()) << "TAA→non-TAA switch must wipe the cache";
}
