// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_asset_path.cpp
/// @brief 3D_E-0874 — a relative asset path names a file under the asset
///        root, never under the working directory.
///
/// Contract: docs/specs/3D_E-0874-asset-relative-paths.md (INV-1, INV-3,
/// INV-4, INV-5). The texture cases need a GL context, because without one
/// every load returns the default texture; they skip where there is none.
#include "gl_test_fixture.h"
#include "resource/resource_manager.h"
#include "test_helpers.h"
#include "utils/asset_path.h"

#include <gtest/gtest.h>
#include <stb_image_write.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

/// A temporary asset root holding textures/a.png, and an empty folder to
/// start from, so a working-directory lookup finds nothing.
struct TempRoot
{
    fs::path base;
    fs::path root;
    fs::path elsewhere;

    TempRoot()
    {
        base = fs::temp_directory_path() / ("vestige_asset_path_" + Testing::vestigeTestStamp());
        root = base / "root";
        elsewhere = base / "elsewhere";
        std::error_code ec;
        fs::remove_all(base, ec);
        fs::create_directories(root / "textures");
        fs::create_directories(elsewhere);
    }

    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;
    TempRoot(TempRoot&&) = delete;
    TempRoot& operator=(TempRoot&&) = delete;

    ~TempRoot()
    {
        std::error_code ec;
        fs::remove_all(base, ec);
    }

    static bool writePng(const fs::path& file)
    {
        const std::array<std::uint8_t, 4> pixel = {255, 0, 0, 255};
        return stbi_write_png(file.string().c_str(), 1, 1, 4, pixel.data(), 4) != 0;
    }
};

class AssetPathTextureTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        m_previousCwd = fs::current_path();
        ASSERT_TRUE(TempRoot::writePng(m_dirs.root / "textures" / "a.png"));
        fs::current_path(m_dirs.elsewhere);
        m_resources.setSandboxRoots({m_dirs.root});
        m_resources.setAssetRoot(m_dirs.root);
    }

    void TearDown() override
    {
        if (!m_previousCwd.empty())
        {
            fs::current_path(m_previousCwd);
        }
    }

    TempRoot m_dirs;
    fs::path m_previousCwd;
    ResourceManager m_resources;
};

} // namespace

// INV-1
TEST_F(AssetPathTextureTest, RelativeTextureLoadsFromRootNotCwd)
{
    auto tex = m_resources.loadTexture("textures/a.png", false);
    ASSERT_NE(tex, nullptr);
    EXPECT_NE(tex, m_resources.getDefaultTexture());
}

// INV-3
TEST_F(AssetPathTextureTest, LegacyAssetsPrefixSharesCacheEntry)
{
    auto legacy = m_resources.loadTexture("assets/textures/a.png", false);
    auto current = m_resources.loadTexture("textures/a.png", false);
    EXPECT_NE(legacy, m_resources.getDefaultTexture());
    EXPECT_EQ(legacy, current);
}

// INV-5, new half. Passes before the change too (the working directory holds
// no outside.png either); it fails only against a resolve that skips the
// sandbox check, which is what it exists to catch.
TEST_F(AssetPathTextureTest, RootRelativeTraversalIsRefused)
{
    ASSERT_TRUE(TempRoot::writePng(m_dirs.base / "outside.png"));
    auto tex = m_resources.loadTexture("../outside.png", false);
    EXPECT_EQ(tex, m_resources.getDefaultTexture());
}

// INV-4
TEST(AssetPathKey, AssetsSubfolderIsNotStripped)
{
    TempRoot dirs;
    fs::create_directories(dirs.root / "assets");
    EXPECT_EQ(AssetPath::toKey(dirs.root, "assets/x.png"), "assets/x.png");
}

TEST(AssetPathKey, LegacyPrefixIsStrippedWithoutAnAssetsSubfolder)
{
    TempRoot dirs;
    EXPECT_EQ(AssetPath::toKey(dirs.root, "assets/textures/a.png"), "textures/a.png");
}

TEST(AssetPathKey, AbsolutePathUnderTheRootBecomesRootRelative)
{
    TempRoot dirs;
    EXPECT_EQ(AssetPath::toKey(dirs.root, (dirs.root / "textures" / "a.png").string()),
              "textures/a.png");
}

TEST(AssetPathKey, KeyHoldsNoDotParts)
{
    TempRoot dirs;
    EXPECT_EQ(AssetPath::toKey(dirs.root, (dirs.root / "models" / ".." / "textures" / "a.png").string()),
              "textures/a.png");
    EXPECT_EQ(AssetPath::toKey(dirs.root, "textures/./a.png"), "textures/a.png");
}

TEST(AssetPathKey, AbsolutePathOutsideTheRootIsKept)
{
    TempRoot dirs;
    const std::string outside = (dirs.base / "outside.png").string();
    EXPECT_EQ(AssetPath::toKey(dirs.root, outside), outside);
}

TEST(AssetPathKey, EmptyRootChangesNothing)
{
    EXPECT_EQ(AssetPath::toKey({}, "assets/textures/a.png"), "assets/textures/a.png");
    EXPECT_EQ(AssetPath::resolve({}, "textures/a.png"), fs::path("textures/a.png"));
}

TEST(AssetPathResolve, RelativeJoinsTheRootAndLegacyPathsToo)
{
    TempRoot dirs;
    EXPECT_EQ(AssetPath::resolve(dirs.root, "textures/a.png"), dirs.root / "textures" / "a.png");
    EXPECT_EQ(AssetPath::resolve(dirs.root, "assets/textures/a.png"),
              dirs.root / "textures" / "a.png");
}
