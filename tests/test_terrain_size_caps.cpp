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

// /test-audit 2026-05-17 Ts19-CG3: a complete reject-list is only one
// half of validator coverage; without a happy-path test the acceptance
// branch is unexercised and a bug like "validator always returns false
// for every input" would slip past the existing reject-only tests.
//
// `deserializeSettings(...)` tail-calls `initialize(config)`, which
// needs a GL context to allocate textures + compile shaders — too
// heavyweight for a validator-only check. The validator was extracted
// into the static `Terrain::validateJsonConfig()` so the acceptance
// branch is directly testable here without bringing GL into the suite.
TEST(TerrainSizeCaps, ValidateJsonConfigAcceptsWithinLimits_CG3)
{
    nlohmann::json j;
    // Smallest valid grid (3×3) with the smallest valid grid resolution
    // and one LOD level — exercises every cap simultaneously.
    j["width"]          = 3;
    j["depth"]          = 3;
    j["gridResolution"] = 3;
    j["maxLodLevels"]   = 1;
    EXPECT_TRUE(Terrain::validateJsonConfig(j));

    // Sanity: the reject-tests above should still see false on these
    // exact same caps, since validateJsonConfig is the source of truth
    // the reject-tests reach through `deserializeSettings`.
    j["width"] = 100000;
    EXPECT_FALSE(Terrain::validateJsonConfig(j));
}

}  // namespace Vestige::TerrainSizeCap::Test
