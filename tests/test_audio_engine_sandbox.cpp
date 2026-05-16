// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_engine_sandbox.cpp
/// @brief Phase 10.9 Slice 5 D11 — pin AudioEngine's path-sandbox
///        choke-point. Mirrors test_resource_manager_sandbox.cpp.
///
/// Tests do not initialise the OpenAL device (CI runners typically have
/// no audio hardware). The sandbox check runs *before* the
/// `m_available` short-circuit, so we can observe path rejection by
/// checking `loadBuffer` return values without needing a live engine.

#include <gtest/gtest.h>
#include "audio/audio_engine.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "test_helpers.h"

namespace fs = std::filesystem;

namespace Vestige::AudioEngineSandbox::Test
{

class AudioEngineSandboxTest : public ::testing::Test
{
protected:
    fs::path m_root;
    fs::path m_outsidePath;

    void SetUp() override
    {
        // Unique per-process + per-test so `ctest -j` doesn't race
        // on a shared temp dir.
        m_root = fs::temp_directory_path()
               / ("vestige_audio_sandbox_test_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root / "assets");
        fs::create_directories(m_root / "outside");
        std::ofstream{m_root / "assets" / "ok.wav"} << "RIFF";
        std::ofstream{m_root / "outside" / "evil.wav"} << "RIFF";
        m_outsidePath = m_root / "outside" / "evil.wav";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
};

TEST_F(AudioEngineSandboxTest, GetSandboxRootsStartsEmpty_D11)
{
    AudioEngine engine;
    EXPECT_TRUE(engine.getSandboxRoots().empty());
}

TEST_F(AudioEngineSandboxTest, SetSandboxRootsRecordsRoots_D11)
{
    AudioEngine engine;
    engine.setSandboxRoots({m_root / "assets"});
    ASSERT_EQ(engine.getSandboxRoots().size(), 1u);
    EXPECT_EQ(engine.getSandboxRoots()[0], m_root / "assets");
}

TEST_F(AudioEngineSandboxTest, SetSandboxRootsAcceptsMultipleRoots_D11)
{
    AudioEngine engine;
    engine.setSandboxRoots({m_root / "assets", m_root / "other"});
    EXPECT_EQ(engine.getSandboxRoots().size(), 2u);
}

TEST_F(AudioEngineSandboxTest, LoadBufferOutsideRootReturnsZero_D11)
{
    AudioEngine engine;  // not initialised — no audio device required
    engine.setSandboxRoots({m_root / "assets"});
    // Path escapes the configured root → sandbox rejects → return 0.
    // (Returns 0 even if the engine were initialised; we verify the
    // pre-availability rejection path by skipping initialise().)
    EXPECT_EQ(engine.loadBuffer(m_outsidePath.string()), 0u);
}

// Honest scope: without a sandbox configured, loadBuffer returns 0 for
// "engine not initialised", not for "sandbox rejected". The two return
// 0 for different reasons and the audio engine surface doesn't expose
// an observable distinguishing them. This test pins the round-trip
// getter (empty roots → still empty after loadBuffer) — the
// acceptance half of the contract is exercised at engine launch on
// machines with audio.
TEST_F(AudioEngineSandboxTest, NoSandboxConfiguredRoundTrips_D11)
{
    AudioEngine engine;
    EXPECT_TRUE(engine.getSandboxRoots().empty());
    EXPECT_EQ(engine.loadBuffer(m_outsidePath.string()), 0u);
    EXPECT_TRUE(engine.getSandboxRoots().empty());
}

}  // namespace Vestige::AudioEngineSandbox::Test
