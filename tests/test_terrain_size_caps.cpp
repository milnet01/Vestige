// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_size_caps.cpp
/// @brief Phase 10.9 Slice 5 D8 — pin Terrain::deserializeSettings size caps.
///
/// `width * depth * sizeof(float)` on uncapped JSON could request up
/// to ~320 GB heap (width=depth=2^17). D8 hard-caps width/depth ≤ 8193
/// and gridResolution ≤ 257 and maxLodLevels ≤ 10 to close the OOM-kill
/// vector. These tests verify the validator runs without GL context —
/// it must short-circuit before `initialize()` (which needs GL).

#include <gtest/gtest.h>
#include "environment/terrain.h"

#include <nlohmann/json.hpp>

namespace Vestige::TerrainSizeCap::Test
{

TEST(TerrainSizeCaps, DeserializeRejectsExcessiveWidth_D8)
{
    Terrain t;
    nlohmann::json j;
    j["width"] = 100000;
    j["depth"] = 8193;
    EXPECT_FALSE(t.deserializeSettings(j));
}

TEST(TerrainSizeCaps, DeserializeRejectsExcessiveDepth_D8)
{
    Terrain t;
    nlohmann::json j;
    j["width"] = 8193;
    j["depth"] = 100000;
    EXPECT_FALSE(t.deserializeSettings(j));
}

TEST(TerrainSizeCaps, DeserializeRejectsNegativeWidth_D8)
{
    Terrain t;
    nlohmann::json j;
    j["width"] = -1;
    EXPECT_FALSE(t.deserializeSettings(j));
}

TEST(TerrainSizeCaps, DeserializeRejectsZeroDimension_D8)
{
    Terrain t;
    nlohmann::json j;
    j["width"] = 0;
    j["depth"] = 0;
    EXPECT_FALSE(t.deserializeSettings(j));
}

TEST(TerrainSizeCaps, DeserializeRejectsExcessiveGridResolution_D8)
{
    Terrain t;
    nlohmann::json j;
    j["width"] = 257;
    j["depth"] = 257;
    j["gridResolution"] = 100000;
    EXPECT_FALSE(t.deserializeSettings(j));
}

TEST(TerrainSizeCaps, DeserializeRejectsExcessiveMaxLodLevels_D8)
{
    Terrain t;
    nlohmann::json j;
    j["width"] = 257;
    j["depth"] = 257;
    j["maxLodLevels"] = 100;
    EXPECT_FALSE(t.deserializeSettings(j));
}

TEST(TerrainSizeCaps, DeserializeRejectsZeroMaxLodLevels_D8)
{
    Terrain t;
    nlohmann::json j;
    j["maxLodLevels"] = 0;
    EXPECT_FALSE(t.deserializeSettings(j));
}

// Note: we do NOT test the happy-path success case here because
// `initialize()` requires a GL context and would crash without one.
// The validator's job is to short-circuit *before* `initialize()`,
// which the tests above pin by asserting `false` is returned for
// every out-of-range case (proving the validator ran and rejected).
// A combined acceptable-input test that exercises `initialize()`
// would belong in a GL-context test harness (R2 follow-up).

}  // namespace Vestige::TerrainSizeCap::Test
