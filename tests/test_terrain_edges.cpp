// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_edges.cpp
/// @brief 3D_E-0858 — the heightmap's edge texels repeat outward
///        (clamp-to-edge), so border normals and the smooth brush do not
///        see a false drop to height 0 outside the grid.

#include "gl_test_fixture.h"

#include "environment/terrain.h"

#include <gtest/gtest.h>

namespace Vestige
{
namespace
{

class TerrainEdgeTest : public Test::GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        m_config.width = 33;
        m_config.depth = 33;
        ASSERT_TRUE(m_terrain.initialize(m_config));
        // A flat plateau well above 0: every normal should point straight up.
        for (int z = 0; z < m_config.depth; ++z)
        {
            for (int x = 0; x < m_config.width; ++x)
            {
                m_terrain.setRawHeight(x, z, 0.6f);
            }
        }
        m_terrain.updateNormalMapRegion(0, 0, m_config.width, m_config.depth);
    }

    TerrainConfig m_config;
    Terrain m_terrain;
};

// Before initialize() there is no grid to clamp into: reads return 0.
TEST(TerrainEdge, UninitialisedReadsReturnZero_0858)
{
    Terrain terrain;
    EXPECT_FLOAT_EQ(terrain.getRawHeight(0, 0), 0.0f);
    EXPECT_FLOAT_EQ(terrain.getRawHeight(-4, 9), 0.0f);
}

TEST_F(TerrainEdgeTest, OutOfRangeReadsRepeatTheEdge_0858)
{
    EXPECT_FLOAT_EQ(m_terrain.getRawHeight(-1, 5), 0.6f);
    EXPECT_FLOAT_EQ(m_terrain.getRawHeight(m_config.width, 5), 0.6f);
    EXPECT_FLOAT_EQ(m_terrain.getRawHeight(5, -3), 0.6f);
    EXPECT_FLOAT_EQ(m_terrain.getRawHeight(5, m_config.depth + 2), 0.6f);
}

TEST_F(TerrainEdgeTest, FlatPlateauHasUprightNormalsAtEveryEdge_0858)
{
    const float maxX = static_cast<float>(m_config.width - 1) * m_config.spacingX;
    const float maxZ = static_cast<float>(m_config.depth - 1) * m_config.spacingZ;
    const float mid = maxX * 0.5f;
    for (const auto& [x, z] : {std::pair{0.0f, mid}, std::pair{maxX, mid},
                               std::pair{mid, 0.0f}, std::pair{mid, maxZ},
                               std::pair{0.0f, 0.0f}, std::pair{maxX, maxZ}})
    {
        const glm::vec3 n = m_terrain.getNormal(x, z);
        EXPECT_NEAR(n.y, 1.0f, 1e-5f) << "at (" << x << ", " << z << ")";
    }
}

} // namespace
} // namespace Vestige
