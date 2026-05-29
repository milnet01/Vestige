// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_spatial_hash.cpp
/// @brief Unit tests for SpatialHash spatial acceleration structure.
#include "physics/spatial_hash.h"

#include <gtest/gtest.h>

using namespace Vestige;

// ===========================================================================
// SpatialHash
// ===========================================================================

TEST(SpatialHashTest, EmptyHash)
{
    SpatialHash hash;
    EXPECT_EQ(hash.getEntryCount(), 0u);
}

TEST(SpatialHashTest, BuildAndQuery)
{
    // 4 particles in a line along X
    std::vector<glm::vec3> positions = {
        {0, 0, 0}, {0.5f, 0, 0}, {1.0f, 0, 0}, {5.0f, 0, 0}
    };

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 1.0f);
    EXPECT_EQ(hash.getEntryCount(), 4u);

    // Query around particle 0 with radius 0.6 — should find particle 1 only
    std::vector<uint32_t> result;
    hash.query(positions[0], 0.6f, 0, result);
    EXPECT_EQ(result.size(), 1u);
    if (!result.empty())
    {
        EXPECT_EQ(result[0], 1u);
    }
}

TEST(SpatialHashTest, QueryFindsAllNearby)
{
    std::vector<glm::vec3> positions = {
        {0, 0, 0}, {0.1f, 0, 0}, {0.2f, 0, 0}, {10.0f, 0, 0}
    };

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 0.5f);

    std::vector<uint32_t> result;
    hash.query(positions[0], 0.3f, 0, result);
    EXPECT_EQ(result.size(), 2u);  // particles 1 and 2
}

TEST(SpatialHashTest, SelfExclusion)
{
    std::vector<glm::vec3> positions = {{0, 0, 0}, {0.01f, 0, 0}};

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 1.0f);

    std::vector<uint32_t> result;
    hash.query(positions[0], 1.0f, 0, result);
    // Should find particle 1 but not 0
    EXPECT_EQ(result.size(), 1u);
    if (!result.empty())
    {
        EXPECT_EQ(result[0], 1u);
    }
}

TEST(SpatialHashTest, DistantParticlesNotFound)
{
    std::vector<glm::vec3> positions = {{0, 0, 0}, {100, 0, 0}};

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 1.0f);

    std::vector<uint32_t> result;
    hash.query(positions[0], 1.0f, 0, result);
    EXPECT_TRUE(result.empty());
}

TEST(SpatialHashTest, ThreeDimensionalQuery)
{
    std::vector<glm::vec3> positions = {
        {0, 0, 0},
        {0.3f, 0.3f, 0.3f},  // dist ~0.52
        {0, 1, 0},            // dist 1.0
    };

    SpatialHash hash;
    hash.build(positions.data(), positions.size(), 0.6f);

    std::vector<uint32_t> result;
    hash.query(positions[0], 0.6f, 0, result);
    EXPECT_EQ(result.size(), 1u);  // only particle 1
}

TEST(SpatialHashTest, CellSizeStored)
{
    SpatialHash hash;
    std::vector<glm::vec3> positions = {{0, 0, 0}};
    hash.build(positions.data(), positions.size(), 0.42f);
    EXPECT_NEAR(hash.getCellSize(), 0.42f, 1e-6f);
}
