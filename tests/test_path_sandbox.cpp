// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_path_sandbox.cpp
/// @brief Phase 10.9 Slice 5 D1 — pin path-traversal guards.

#include <gtest/gtest.h>
#include "utils/path_sandbox.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::PathSandbox::Test
{

class PathSandboxTest : public ::testing::Test
{
protected:
    fs::path m_root;

    void SetUp() override
    {
        // Unique per-process + per-test so `ctest -j` doesn't race on
        // a shared temp dir.
        m_root = fs::temp_directory_path()
               / ("vestige_path_sandbox_test_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root / "assets" / "ok");
        fs::create_directories(m_root / "sibling");
        std::ofstream{m_root / "assets" / "ok" / "in.png"} << "ok";
        std::ofstream{m_root / "sibling" / "out.png"} << "out";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
};

TEST_F(PathSandboxTest, ResolveUriIntoBaseAcceptsFileInsideBase_D1)
{
    auto out = resolveUriIntoBase(m_root / "assets", "ok/in.png");
    EXPECT_NE(out, "");
    EXPECT_NE(out.find("ok"), std::string::npos);
}

TEST_F(PathSandboxTest, ResolveUriIntoBaseRejectsParentTraversal_D1)
{
    auto out = resolveUriIntoBase(m_root / "assets", "../sibling/out.png");
    EXPECT_EQ(out, "");
}

TEST_F(PathSandboxTest, ResolveUriIntoBaseRejectsSiblingPrefixCollision_D1)
{
    // AUDIT M16: base="/x/foo", canonical="/x/foo_evil/y" must NOT pass.
    fs::create_directories(m_root / "assets_evil");
    std::ofstream{m_root / "assets_evil" / "x.png"} << "x";

    auto out = resolveUriIntoBase(m_root / "assets", "../assets_evil/x.png");
    EXPECT_EQ(out, "");
}

TEST_F(PathSandboxTest, ResolveUriIntoBaseEmptyUriReturnsEmpty_D1)
{
    auto out = resolveUriIntoBase(m_root / "assets", "");
    EXPECT_EQ(out, "");
}

TEST_F(PathSandboxTest, ResolveUriIntoBaseEqualsBaseAcceptsBaseItself_D1)
{
    auto out = resolveUriIntoBase(m_root / "assets", ".");
    EXPECT_NE(out, "");
}

TEST_F(PathSandboxTest, ValidateInsideRootsAcceptsAbsolutePathInsideRoot_D1)
{
    auto abs = m_root / "assets" / "ok" / "in.png";
    auto out = validateInsideRoots(abs, {m_root / "assets"});
    EXPECT_NE(out, "");
}

TEST_F(PathSandboxTest, ValidateInsideRootsRejectsAbsolutePathOutsideRoot_D1)
{
    auto abs = m_root / "sibling" / "out.png";
    auto out = validateInsideRoots(abs, {m_root / "assets"});
    EXPECT_EQ(out, "");
}

TEST_F(PathSandboxTest, ValidateInsideRootsAcceptsAnyOfMultipleRoots_D1)
{
    auto abs = m_root / "sibling" / "out.png";
    auto out = validateInsideRoots(abs, {m_root / "assets", m_root / "sibling"});
    EXPECT_NE(out, "");
}

TEST_F(PathSandboxTest, ValidateInsideRootsEmptyRootsReturnsCanonUnchanged_D1)
{
    // Backwards-compat: no roots configured → no sandbox active.
    auto abs = m_root / "sibling" / "out.png";
    auto out = validateInsideRoots(abs, {});
    EXPECT_NE(out, "");
}

TEST_F(PathSandboxTest, ValidateInsideRootsRejectsSiblingPrefixCollision_D1)
{
    fs::create_directories(m_root / "assets_evil");
    std::ofstream{m_root / "assets_evil" / "x.png"} << "x";

    auto abs = m_root / "assets_evil" / "x.png";
    auto out = validateInsideRoots(abs, {m_root / "assets"});
    EXPECT_EQ(out, "");
}

}  // namespace Vestige::PathSandbox::Test
