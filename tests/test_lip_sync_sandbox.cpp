// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_lip_sync_sandbox.cpp
/// @brief Phase 10.9 Slice 5 D11 — pin LipSyncPlayer's path-sandbox
///        for `loadTrack`.
///
/// LipSyncPlayer is in `engine/experimental/animation/` (relocated by
/// W12) but the test still runs as part of the regular suite because
/// the experimental README permits test files to include from there.
///
/// The sandbox roots are stored in a process-wide static; tests reset
/// to empty in TearDown so they don't leak state between cases.

#include <gtest/gtest.h>
#include "experimental/animation/lip_sync.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::LipSyncSandbox::Test
{

class LipSyncSandboxTest : public ::testing::Test
{
protected:
    fs::path m_root;
    fs::path m_insidePath;
    fs::path m_outsidePath;

    void SetUp() override
    {
        // Unique per-process + per-test so `ctest -j` doesn't race
        // on a shared temp dir.
        m_root = fs::temp_directory_path()
               / ("vestige_lipsync_sandbox_test_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root / "assets");
        fs::create_directories(m_root / "outside");
        // Empty JSON-like stubs — the sandbox check runs before the
        // JSON parser so the contents don't need to be valid Rhubarb.
        std::ofstream{m_root / "assets" / "ok.json"} << "{}";
        std::ofstream{m_root / "outside" / "evil.json"} << "{}";
        m_insidePath  = m_root / "assets" / "ok.json";
        m_outsidePath = m_root / "outside" / "evil.json";
    }

    void TearDown() override
    {
        // Reset the process-wide sandbox so subsequent tests aren't affected.
        LipSyncPlayer::setSandboxRoots({});
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
};

TEST_F(LipSyncSandboxTest, GetSandboxRootsStartsEmpty_D11)
{
    LipSyncPlayer::setSandboxRoots({});
    EXPECT_TRUE(LipSyncPlayer::getSandboxRoots().empty());
}

TEST_F(LipSyncSandboxTest, SetSandboxRootsRecordsRoots_D11)
{
    LipSyncPlayer::setSandboxRoots({m_root / "assets"});
    ASSERT_EQ(LipSyncPlayer::getSandboxRoots().size(), 1u);
    EXPECT_EQ(LipSyncPlayer::getSandboxRoots()[0], m_root / "assets");
}

TEST_F(LipSyncSandboxTest, LoadTrackOutsideRootReturnsFalse_D11)
{
    LipSyncPlayer::setSandboxRoots({m_root / "assets"});
    LipSyncPlayer player;
    EXPECT_FALSE(player.loadTrack(m_outsidePath.string()));
}

TEST_F(LipSyncSandboxTest, LoadTrackInsideRootDoesNotCrash_D11)
{
    // Smoke: with sandbox configured to the assets dir, loadTrack on a
    // file inside that dir bypasses the path-rejection branch and runs
    // the JSON-parse path. We can't differentiate parse-failure vs
    // sandbox-failure return codes without finer-grained observability,
    // so the pin here is "did not crash, did not mutate sandbox state".
    LipSyncPlayer::setSandboxRoots({m_root / "assets"});
    LipSyncPlayer player;
    [[maybe_unused]] bool result = player.loadTrack(m_insidePath.string());
    EXPECT_EQ(LipSyncPlayer::getSandboxRoots().size(), 1u);
}

TEST_F(LipSyncSandboxTest, LoadTrackWithoutSandboxConfigDoesNotCrash_D11)
{
    // Smoke: with no sandbox configured, loadTrack on any path is
    // forwarded to the file-size + JSON parsers. Pin is "did not crash,
    // did not retro-configure sandbox state".
    LipSyncPlayer::setSandboxRoots({});
    LipSyncPlayer player;
    [[maybe_unused]] bool result = player.loadTrack(m_outsidePath.string());
    EXPECT_TRUE(LipSyncPlayer::getSandboxRoots().empty());
}

}  // namespace Vestige::LipSyncSandbox::Test
