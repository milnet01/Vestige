// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_material_set.cpp
/// @brief Slice A1 unit tests — height-blend parity (pure) + TerrainMaterialSet load.
///
/// The blend tests are GL-free (pure header). The load tests need a GL context and
/// generate their own tiny fixture images at runtime via stb_image_write, so no binary
/// fixtures are committed.
#include "environment/terrain_material_blend.h"
#include "environment/terrain_material_set.h"

#include "gl_test_fixture.h"

#include <stb_image_write.h>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

using namespace Vestige;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Height-blend parity (pure, no GL) — design §7.
// ---------------------------------------------------------------------------

namespace
{
float sum4(const std::array<float, 4>& a)
{
    return std::accumulate(a.begin(), a.end(), 0.0f);
}
}  // namespace

TEST(TerrainHeightBlendTest, SingleDominantLayerWinsOutright)
{
    // Layer 0 has both the highest height and the only non-zero splat weight.
    std::array<float, 4> h{0.9f, 0.1f, 0.1f, 0.1f};
    std::array<float, 4> w{1.0f, 0.0f, 0.0f, 0.0f};
    auto res = heightBlendWeights(h, w, TERRAIN_HEIGHT_BLEND_DEPTH);
    EXPECT_NEAR(res[0], 1.0f, 1e-5f);
    EXPECT_NEAR(res[1], 0.0f, 1e-5f);
    EXPECT_NEAR(res[2], 0.0f, 1e-5f);
    EXPECT_NEAR(res[3], 0.0f, 1e-5f);
}

TEST(TerrainHeightBlendTest, EqualHeightsAndWeightsSplitEvenly)
{
    std::array<float, 4> h{0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> w{0.5f, 0.5f, 0.5f, 0.5f};
    auto res = heightBlendWeights(h, w, TERRAIN_HEIGHT_BLEND_DEPTH);
    for (float v : res)
    {
        EXPECT_NEAR(v, 0.25f, 1e-5f);
    }
}

TEST(TerrainHeightBlendTest, HighHeightLowWeightStillPeeksThrough)
{
    // Layer 1 has a lower splat weight but a taller height — within the blend band it
    // must still contribute (the whole point of height-blend over linear alpha).
    std::array<float, 4> h{0.30f, 0.55f, 0.0f, 0.0f};
    std::array<float, 4> w{0.60f, 0.40f, 0.0f, 0.0f};
    auto res = heightBlendWeights(h, w, TERRAIN_HEIGHT_BLEND_DEPTH);
    // hw = {0.90, 0.95}; ma = 0.95 - 0.2 = 0.75; b = {0.15, 0.20}; sum 0.35.
    EXPECT_NEAR(res[0], 0.15f / 0.35f, 1e-5f);
    EXPECT_NEAR(res[1], 0.20f / 0.35f, 1e-5f);
    EXPECT_GT(res[1], res[0]);  // the taller layer dominates despite lower weight
    EXPECT_GT(res[0], 0.0f);    // ...but the lower layer still peeks through
}

TEST(TerrainHeightBlendTest, WeightsSumToOneAndAreNonNegative)
{
    // A spread of asymmetric inputs; the normalisation invariant must always hold.
    const std::array<std::array<float, 4>, 3> heights{{
        {0.1f, 0.2f, 0.3f, 0.4f},
        {0.9f, 0.05f, 0.5f, 0.5f},
        {0.0f, 1.0f, 0.25f, 0.75f},
    }};
    const std::array<std::array<float, 4>, 3> weights{{
        {0.25f, 0.25f, 0.25f, 0.25f},
        {0.7f, 0.1f, 0.1f, 0.1f},
        {0.4f, 0.3f, 0.2f, 0.1f},
    }};
    for (size_t c = 0; c < heights.size(); ++c)
    {
        auto res = heightBlendWeights(heights[c], weights[c], TERRAIN_HEIGHT_BLEND_DEPTH);
        EXPECT_NEAR(sum4(res), 1.0f, 1e-5f);
        for (float v : res)
        {
            EXPECT_GE(v, 0.0f);
        }
    }
}

TEST(TerrainHeightBlendTest, SmallestSupportedDepthHasSafeDivisor)
{
    // At the smallest supported depth the peak layer contributes b_peak = depth, so the
    // divisor Σb ≥ depth > 0 — no NaN/Inf, weights still valid.
    std::array<float, 4> h{0.6f, 0.4f, 0.2f, 0.1f};
    std::array<float, 4> w{0.5f, 0.3f, 0.15f, 0.05f};
    auto res = heightBlendWeights(h, w, TERRAIN_HEIGHT_BLEND_DEPTH_MIN);
    EXPECT_NEAR(sum4(res), 1.0f, 1e-5f);
    for (float v : res)
    {
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(v, 1.0f);
    }
}

#ifndef NDEBUG
TEST(TerrainHeightBlendDeathTest, DepthZeroTripsTheDebugAssert)
{
    // depth = 0 is out of contract (0/0). The debug assert must fire rather than the
    // function returning a defined-but-meaningless blend.
    std::array<float, 4> h{0.5f, 0.5f, 0.5f, 0.5f};
    std::array<float, 4> w{0.5f, 0.5f, 0.5f, 0.5f};
    EXPECT_DEATH({ (void)heightBlendWeights(h, w, 0.0f); }, "depth must be > 0");
}
#endif

// ---------------------------------------------------------------------------
// TerrainMaterialSet load (needs a GL context) — design §7.
// ---------------------------------------------------------------------------

namespace
{
/// @brief Writes a solid W×H×channels PNG to `path`; returns false on write failure.
bool writeTestPng(const fs::path& path, int w, int h, int channels, unsigned char fill)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * static_cast<size_t>(h)
                                          * static_cast<size_t>(channels),
                                      fill);
    return stbi_write_png(path.string().c_str(), w, h, channels, pixels.data(),
                          w * channels) != 0;
}
}  // namespace

class TerrainMaterialSetTest : public Vestige::Test::GLTestFixture
{
protected:
    void SetUp() override
    {
        Vestige::Test::GLTestFixture::SetUp();  // GTEST_SKIP() if no GL context
        m_dir = fs::temp_directory_path() / "vestige_terrain_matset_test";
        std::error_code ec;
        fs::create_directories(m_dir, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    /// @brief Builds a layer whose three maps are `size`×`size` PNGs.
    TerrainLayerDesc makeLayer(const std::string& tag, int size)
    {
        TerrainLayerDesc d;
        d.albedoPath = (m_dir / (tag + "_albedo.png")).string();
        d.normalPath = (m_dir / (tag + "_normal.png")).string();
        d.materialPath = (m_dir / (tag + "_material.png")).string();
        d.tiling = 0.2f;
        writeTestPng(d.albedoPath, size, size, 3, 120);
        writeTestPng(d.normalPath, size, size, 3, 128);   // flat +Z normal-ish
        writeTestPng(d.materialPath, size, size, 3, 200);
        return d;
    }

    fs::path m_dir;
};

TEST_F(TerrainMaterialSetTest, ValidFourLayerSetBuildsThreeArrays)
{
    std::array<TerrainLayerDesc, 4> layers{
        makeLayer("grass", 8), makeLayer("rock", 8),
        makeLayer("dirt", 8), makeLayer("sand", 8)};

    TerrainMaterialSet set;
    ASSERT_TRUE(set.load(layers));
    EXPECT_TRUE(set.isValid());
    EXPECT_NE(set.albedoArray(), 0u);
    EXPECT_NE(set.normalArray(), 0u);
    EXPECT_NE(set.materialArray(), 0u);
    EXPECT_FLOAT_EQ(set.tilings()[0], 0.2f);
}

TEST_F(TerrainMaterialSetTest, DimensionMismatchFailsSoftToFallback)
{
    // Layer 2 ("dirt") is 4×4 while the rest are 8×8 → the array requirement is violated.
    std::array<TerrainLayerDesc, 4> layers{
        makeLayer("grass", 8), makeLayer("rock", 8),
        makeLayer("dirt", 4), makeLayer("sand", 8)};

    TerrainMaterialSet set;
    EXPECT_FALSE(set.load(layers));
    EXPECT_FALSE(set.isValid());
    EXPECT_EQ(set.albedoArray(), 0u);
    EXPECT_EQ(set.normalArray(), 0u);
    EXPECT_EQ(set.materialArray(), 0u);
}

TEST_F(TerrainMaterialSetTest, MissingFileFailsSoftToFallback)
{
    // A distinct code path from the dimension check: stb_image decode returns null.
    std::array<TerrainLayerDesc, 4> layers{
        makeLayer("grass", 8), makeLayer("rock", 8),
        makeLayer("dirt", 8), makeLayer("sand", 8)};
    layers[1].normalPath = (m_dir / "does_not_exist.png").string();

    TerrainMaterialSet set;
    EXPECT_FALSE(set.load(layers));
    EXPECT_FALSE(set.isValid());
    EXPECT_EQ(set.albedoArray(), 0u);
}

TEST_F(TerrainMaterialSetTest, DefaultConstructedSetIsInvalid)
{
    TerrainMaterialSet set;
    EXPECT_FALSE(set.isValid());
    EXPECT_EQ(set.albedoArray(), 0u);
}
