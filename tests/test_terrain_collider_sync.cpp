// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_collider_sync.cpp
/// @brief When the terrain's physics collider is rebuilt after an edit
///        (3D_E-0780), and the height revision that drives it.

#include <gtest/gtest.h>

#include "gl_test_fixture.h"
#include "environment/terrain.h"
#include "physics/terrain_collider_sync.h"

using namespace Vestige;

TEST(TerrainColliderSync, NoRebuildWhileNothingChanged)
{
    TerrainColliderSync sync;
    sync.markBuilt(3);
    EXPECT_FALSE(sync.shouldRebuild(3));
    EXPECT_FALSE(sync.shouldRebuild(3));
}

TEST(TerrainColliderSync, OneEditRebuildsTheFrameAfter)
{
    TerrainColliderSync sync;
    sync.markBuilt(3);
    EXPECT_FALSE(sync.shouldRebuild(4)) << "the frame of the edit: not yet settled";
    EXPECT_TRUE(sync.shouldRebuild(4)) << "the next frame: settled, so rebuild";
    sync.markBuilt(4);
    EXPECT_FALSE(sync.shouldRebuild(4)) << "rebuilt once, not every frame after";
}

TEST(TerrainColliderSync, StrokeRebuildsOnceAfterItEnds)
{
    TerrainColliderSync sync;
    sync.markBuilt(0);
    int rebuilds = 0;
    // A stroke: the revision moves every frame for ten frames.
    for (std::uint64_t rev = 1; rev <= 10; ++rev)
    {
        if (sync.shouldRebuild(rev))
        {
            ++rebuilds;
            sync.markBuilt(rev);
        }
    }
    EXPECT_EQ(rebuilds, 0) << "rebuilt mid-stroke";
    for (int frame = 0; frame < 5; ++frame)
    {
        if (sync.shouldRebuild(10))
        {
            ++rebuilds;
            sync.markBuilt(10);
        }
    }
    EXPECT_EQ(rebuilds, 1);
}

class TerrainHeightRevisionTest : public ::Vestige::Test::GLTestFixture {};

TEST_F(TerrainHeightRevisionTest, EveryHeightUploadBumpsTheRevision)
{
    Terrain terrain;
    ASSERT_TRUE(terrain.initialize(TerrainConfig{}));

    const std::uint64_t before = terrain.getHeightRevision();
    terrain.setRawHeight(4, 4, 0.5f);
    terrain.updateHeightmapRegion(4, 4, 1, 1);
    EXPECT_GT(terrain.getHeightRevision(), before);

    const std::uint64_t mid = terrain.getHeightRevision();
    terrain.updateHeightmapRegion(0, 0, terrain.getWidth(), terrain.getDepth());
    EXPECT_GT(terrain.getHeightRevision(), mid);
}
