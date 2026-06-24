// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_asset_locator.cpp
/// @brief GL-free unit tests for the asset-root search order (asset_locator.h).
///        The exe-dir probe + real filesystem are not exercised here; the pure
///        ordering + selection are, with the validity predicate stubbed.

#include "utils/asset_locator.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace Vestige::Test
{

namespace fs = std::filesystem;

TEST(AssetLocator, CandidateOrderPrefersBinaryRelative)
{
    auto c = assetSearchCandidates("/opt/vestige", "/home/u/proj");
    ASSERT_EQ(c.size(), 3u);
    EXPECT_EQ(c[0], fs::path("/opt/vestige/assets"));
    EXPECT_EQ(c[1], fs::path("/opt/vestige") / ".." / "share" / "vestige" / "assets");
    EXPECT_EQ(c[2], fs::path("/home/u/proj/assets"));
}

TEST(AssetLocator, EmptyExeDirOmitsBinaryRelativeCandidates)
{
    auto c = assetSearchCandidates("", "/home/u/proj");
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0], fs::path("/home/u/proj/assets"));
}

TEST(AssetLocator, FirstValidWins)
{
    std::vector<fs::path> cands = {"/a/assets", "/b/assets", "/c/assets"};
    // Only /b is "valid".
    auto pick = firstValidAssetDir(cands, [](const fs::path& p) {
        return p == fs::path("/b/assets");
    });
    EXPECT_EQ(pick, fs::path("/b/assets"));
}

TEST(AssetLocator, FirstValidPicksEarliestWhenMultipleValid)
{
    std::vector<fs::path> cands = {"/a/assets", "/b/assets"};
    auto pick = firstValidAssetDir(cands, [](const fs::path&) { return true; });
    EXPECT_EQ(pick, fs::path("/a/assets")) << "earliest candidate must win";
}

TEST(AssetLocator, NoneValidReturnsEmpty)
{
    std::vector<fs::path> cands = {"/a/assets", "/b/assets"};
    auto pick = firstValidAssetDir(cands, [](const fs::path&) { return false; });
    EXPECT_TRUE(pick.empty());
}

TEST(AssetLocator, ExplicitOverrideIsHonouredAsIs)
{
    // A non-empty --assets value is returned verbatim, not auto-searched.
    EXPECT_EQ(resolveAssetPath("/custom/assets"), "/custom/assets");
}

TEST(AssetLocator, IsAssetDirChecksSentinelShader)
{
    // A temp dir without the sentinel is not an asset dir; with it, it is.
    fs::path base = fs::temp_directory_path() / "vestige_asset_locator_test";
    fs::remove_all(base);
    fs::create_directories(base / "shaders");
    EXPECT_FALSE(isAssetDir(base)) << "missing sentinel shader";
    std::ofstream(base / "shaders" / "scene.vert.glsl") << "// sentinel\n";
    EXPECT_TRUE(isAssetDir(base)) << "sentinel present";
    fs::remove_all(base);
}

} // namespace Vestige::Test
