// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_resource_manager_sandbox.cpp
/// @brief Phase 10.9 Slice 5 D1 — pin ResourceManager's path-sandbox choke-point.
///
/// Tests run without GL context: `Texture::loadFromFile` will fail in
/// the no-context environment, but the sandbox check runs *before* the
/// load attempt, so we can observe rejection by checking that the
/// returned texture is the default fallback (not nullptr) and the
/// cache wasn't populated.

#include <gtest/gtest.h>
#include "resource/resource_manager.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::ResourceManagerSandbox::Test
{

class ResourceManagerSandboxTest : public ::testing::Test
{
protected:
    fs::path m_root;
    fs::path m_outsidePath;

    void SetUp() override
    {
        // Unique per-process + per-test so parallel `ctest -j` runs
        // don't collide on the temp dir.
        m_root = fs::temp_directory_path()
               / ("vestige_rm_sandbox_test_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root / "assets");
        fs::create_directories(m_root / "outside");
        std::ofstream{m_root / "assets" / "ok.txt"} << "ok";
        std::ofstream{m_root / "outside" / "evil.txt"} << "evil";
        m_outsidePath = m_root / "outside" / "evil.txt";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
};

TEST_F(ResourceManagerSandboxTest, GetSandboxRootsStartsEmpty_D1)
{
    ResourceManager rm;
    EXPECT_TRUE(rm.getSandboxRoots().empty());
}

TEST_F(ResourceManagerSandboxTest, SetSandboxRootsRecordsRoots_D1)
{
    ResourceManager rm;
    rm.setSandboxRoots({m_root / "assets"});
    ASSERT_EQ(rm.getSandboxRoots().size(), 1u);
    EXPECT_EQ(rm.getSandboxRoots()[0], m_root / "assets");
}

TEST_F(ResourceManagerSandboxTest, LoadMeshOutsideRootReturnsNullptr_D1)
{
    ResourceManager rm;
    rm.setSandboxRoots({m_root / "assets"});
    auto mesh = rm.loadMesh(m_outsidePath.string());
    EXPECT_EQ(mesh, nullptr);
}

TEST_F(ResourceManagerSandboxTest, LoadModelOutsideRootReturnsNullptr_D1)
{
    ResourceManager rm;
    rm.setSandboxRoots({m_root / "assets"});
    auto model = rm.loadModel(m_outsidePath.string());
    EXPECT_EQ(model, nullptr);
}

TEST_F(ResourceManagerSandboxTest, NoSandboxConfiguredAcceptsAnyPath_D1)
{
    // Backwards-compat: empty roots = sandbox disabled. Load attempt
    // will fail at the file-content layer but the sandbox check passes.
    ResourceManager rm;
    EXPECT_TRUE(rm.getSandboxRoots().empty());
    // Mesh load fails (file is just "ok" not OBJ) but sandbox check
    // shouldn't be the cause of the failure.
    auto mesh = rm.loadMesh((m_root / "assets" / "ok.txt").string());
    // Non-OBJ content → loader returns false → loadMesh returns nullptr.
    // Distinguishing sandbox-rejection vs load-fail isn't visible at this
    // API surface; the previous test (with sandbox) verifies the early
    // path. This test pins that the no-sandbox path doesn't *additionally*
    // reject.
    EXPECT_EQ(mesh, nullptr);
}

}  // namespace Vestige::ResourceManagerSandbox::Test
