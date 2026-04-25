// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cube_loader_hardening.cpp
/// @brief Phase 10.9 Slice 5 D3 — pin CubeLoader's path-sandbox
///        choke-point. The 128 MB file-size cap is delegated to
///        `JsonSizeCap::loadTextFileWithSizeCap` (covered by its own
///        tests); this file pins the new cube-side wiring.

#include <gtest/gtest.h>
#include "utils/cube_loader.h"
#include "core/logger.h"

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#define VESTIGE_GETPID() _getpid()
#else
#include <unistd.h>
#define VESTIGE_GETPID() getpid()
#endif

namespace fs = std::filesystem;

namespace Vestige::CubeLoaderHardening::Test
{

constexpr const char* kMinimalCube =
    "TITLE \"identity\"\n"
    "LUT_3D_SIZE 2\n"
    "0 0 0\n"
    "1 0 0\n"
    "0 1 0\n"
    "1 1 0\n"
    "0 0 1\n"
    "1 0 1\n"
    "0 1 1\n"
    "1 1 1\n";

class CubeLoaderHardeningTest : public ::testing::Test
{
protected:
    fs::path m_root;
    fs::path m_insidePath;
    fs::path m_outsidePath;

    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string testName = info ? info->name() : "unknown";
        m_root = fs::temp_directory_path()
               / ("vestige_cube_hardening_test_"
                  + std::to_string(VESTIGE_GETPID()) + "_" + testName);
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root / "assets");
        fs::create_directories(m_root / "outside");
        std::ofstream{m_root / "assets" / "ok.cube"} << kMinimalCube;
        std::ofstream{m_root / "outside" / "evil.cube"} << kMinimalCube;
        m_insidePath  = m_root / "assets" / "ok.cube";
        m_outsidePath = m_root / "outside" / "evil.cube";
        Logger::clearEntries();
    }

    void TearDown() override
    {
        // Reset process-wide sandbox so subsequent tests aren't affected.
        CubeLoader::setSandboxRoots({});
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
};

TEST_F(CubeLoaderHardeningTest, GetSandboxRootsStartsEmpty_D3)
{
    CubeLoader::setSandboxRoots({});
    EXPECT_TRUE(CubeLoader::getSandboxRoots().empty());
}

TEST_F(CubeLoaderHardeningTest, SetSandboxRootsRecordsRoots_D3)
{
    CubeLoader::setSandboxRoots({m_root / "assets"});
    ASSERT_EQ(CubeLoader::getSandboxRoots().size(), 1u);
    EXPECT_EQ(CubeLoader::getSandboxRoots()[0], m_root / "assets");
}

TEST_F(CubeLoaderHardeningTest, LoadOutsideRootReturnsEmpty_D3)
{
    CubeLoader::setSandboxRoots({m_root / "assets"});
    CubeData data = CubeLoader::load(m_outsidePath.string());
    EXPECT_EQ(data.size, 0);
    EXPECT_TRUE(data.rgbaData.empty());
}

TEST_F(CubeLoaderHardeningTest, LoadInsideRootSucceeds_D3)
{
    CubeLoader::setSandboxRoots({m_root / "assets"});
    CubeData data = CubeLoader::load(m_insidePath.string());
    EXPECT_EQ(data.size, 2);
    // RGBA8 for 2³ = 8 entries × 4 bytes.
    EXPECT_EQ(data.rgbaData.size(), 32u);
    EXPECT_EQ(data.title, "identity");
}

TEST_F(CubeLoaderHardeningTest, NoSandboxConfiguredAcceptsAnyPath_D3)
{
    // Sandbox disabled — outside path is forwarded to the size-cap +
    // parse path. The file is well-formed and fits the cap, so it loads.
    CubeLoader::setSandboxRoots({});
    CubeData data = CubeLoader::load(m_outsidePath.string());
    EXPECT_EQ(data.size, 2);
    EXPECT_TRUE(CubeLoader::getSandboxRoots().empty());
}

}  // namespace Vestige::CubeLoaderHardening::Test
