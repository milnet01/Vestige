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

// --- Part B: chooseAssetRoot (pure, injected predicate + picker) -------------

TEST(AssetRoot, AutoResolvedWinsWithoutPicking)
{
    bool pickerCalled = false;
    auto choice = chooseAssetRoot(
        "/auto/assets", "/remembered",
        [](const std::string&) { return true; },
        [&pickerCalled]() { pickerCalled = true; return std::string{}; });
    EXPECT_EQ(choice.path, "/auto/assets");
    EXPECT_FALSE(choice.persist);
    EXPECT_FALSE(pickerCalled) << "picker must not run when auto-resolve succeeded";
}

TEST(AssetRoot, RememberedUsedOnlyWhenValid)
{
    // Valid remembered → used, not persisted, no pick.
    auto good = chooseAssetRoot(
        "", "/remembered",
        [](const std::string& p) { return p == "/remembered"; },
        []() { return std::string{}; });
    EXPECT_EQ(good.path, "/remembered");
    EXPECT_FALSE(good.persist);

    // Invalid remembered → falls through to the picker.
    bool pickerCalled = false;
    auto bad = chooseAssetRoot(
        "", "/stale",
        [](const std::string& p) { return p == "/picked"; },
        [&pickerCalled]() { pickerCalled = true; return std::string{"/picked"}; });
    EXPECT_TRUE(pickerCalled);
    EXPECT_EQ(bad.path, "/picked");
    EXPECT_TRUE(bad.persist) << "a fresh valid pick is persisted";
}

TEST(AssetRoot, PickerLoopsPastInvalidPickToValid)
{
    int calls = 0;
    auto choice = chooseAssetRoot(
        "", "",
        [](const std::string& p) { return p == "/good"; },
        [&calls]() {
            ++calls;
            return std::string(calls < 3 ? "/bad" : "/good");  // 2 bad, then good
        });
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(choice.path, "/good");
    EXPECT_TRUE(choice.persist);
}

TEST(AssetRoot, CancelOrUnavailableYieldsEmpty)
{
    auto choice = chooseAssetRoot(
        "", "",
        [](const std::string&) { return true; },
        []() { return std::string{}; });  // "" = cancel / no dialog
    EXPECT_TRUE(choice.path.empty());
    EXPECT_FALSE(choice.persist);
}

// --- Part B: parseAssetPathConfig (pure) ------------------------------------

TEST(AssetRootConfig, ParsesAssetPathValue)
{
    auto v = parseAssetPathConfig("assets.path=/opt/game/assets\n");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "/opt/game/assets");
}

TEST(AssetRootConfig, PreservesValueContainingEquals)
{
    auto v = parseAssetPathConfig("assets.path=/weird=dir/assets");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "/weird=dir/assets") << "split on first = only";
}

TEST(AssetRootConfig, StripsTrailingCarriageReturn)
{
    auto v = parseAssetPathConfig("assets.path=/x\r\n");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "/x");
}

TEST(AssetRootConfig, SkipsUnknownAndKeylessLines)
{
    auto v = parseAssetPathConfig("# comment\nother=1\nassets.path=/y\n");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "/y");
}

TEST(AssetRootConfig, EmptyOrMissingKeyOrEmptyValueIsNullopt)
{
    EXPECT_FALSE(parseAssetPathConfig("").has_value());
    EXPECT_FALSE(parseAssetPathConfig("other=1\nfoo\n").has_value());
    EXPECT_FALSE(parseAssetPathConfig("assets.path=\n").has_value());
}

} // namespace Vestige::Test
