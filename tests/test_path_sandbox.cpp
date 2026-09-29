// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_path_sandbox.cpp
/// @brief Phase 10.9 Slice 5 D1 — pin path-traversal guards.

#include <gtest/gtest.h>
#include "utils/path_sandbox.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

// Slice 18 Ts4: dropped `ResolveUriIntoBaseRejectsSiblingPrefixCollision_D1`
// — `ValidateInsideRootsRejectsSiblingPrefixCollision_D1` below is the
// canonical pin for the M16 sibling-prefix rule. Both call sites
// route through the same underlying canonical-path comparison; if
// one regresses, both do.

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

// A folder the user links into the asset tree (assets/models/nature_local/*
// -> "/mnt/Games/3D Engine Assets/...") must load. The engine installs
// linkedTargets(assetRoot) as extra roots; before that, every tree and prop
// in the meadow was refused as "escapes sandbox".
class PathSandboxLinkTest : public PathSandboxTest
{
protected:
    void SetUp() override
    {
        PathSandboxTest::SetUp();
        fs::create_directories(m_root / "library" / "trees");
        std::ofstream{m_root / "library" / "trees" / "tree.gltf"} << "t";
        std::error_code ec;
        fs::create_directory_symlink(m_root / "library" / "trees",
                                     m_root / "assets" / "ok" / "linked", ec);
        if (ec)
            GTEST_SKIP() << "cannot create a symlink here: " << ec.message();
    }

    std::vector<fs::path> rootsWithLinks() const
    {
        std::vector<fs::path> roots{m_root / "assets"};
        for (const auto& t : linkedTargets(m_root / "assets"))
            roots.push_back(t);
        return roots;
    }
};

TEST_F(PathSandboxLinkTest, FileThroughLinkInsideRootIsAccepted)
{
    auto p = m_root / "assets" / "ok" / "linked" / "tree.gltf";
    EXPECT_EQ(validateInsideRoots(p, {m_root / "assets"}), "");  // the old refusal
    EXPECT_NE(validateInsideRoots(p, rootsWithLinks()), "");
}

TEST_F(PathSandboxLinkTest, ParentTraversalOutOfLinkTargetIsStillRejected)
{
    // library/trees/.. is library/, and ../../sibling leaves it entirely.
    auto p = m_root / "assets" / "ok" / "linked" / ".." / ".." / "sibling" / "out.png";
    EXPECT_EQ(validateInsideRoots(p, rootsWithLinks()), "");
}

TEST_F(PathSandboxLinkTest, LinksOutsideTheRootAreNotTrusted)
{
    std::error_code ec;
    fs::create_directory_symlink(m_root / "library", m_root / "sibling" / "lib", ec);
    ASSERT_FALSE(ec) << ec.message();
    auto targets = linkedTargets(m_root / "assets");
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets[0], fs::weakly_canonical(m_root / "library" / "trees"));
}

}  // namespace Vestige::PathSandbox::Test
