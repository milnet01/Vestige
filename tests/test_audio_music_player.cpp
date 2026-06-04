// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_music_player.cpp
/// @brief Phase 10.9 Slice 8 W8 (part 2/2) — streaming-music player tests.
///
/// Runs headless: the AudioEngine is constructed but never `initialize()`d,
/// so `isAvailable()` stays false. stb_vorbis decoding is pure file IO and
/// works without a device, so the decode / loop / back-pressure state machine
/// is fully exercised; the AL queue calls short-circuit. Per the design's
/// reconciliation (`docs/phases/phase_10_audio_music_player_design.md` ##
/// Status, 2026-06-04), headless playback advances the consume counter by
/// `dt × sampleRate`, so the planner progresses deterministically.
///
/// Fixture: tests/fixtures/audio/music_loop_test.ogg — 0.5 s, 48 kHz stereo
/// (24000 frames). Test-row numbers map to the design's §13 test plan.

#include <gtest/gtest.h>

#include "audio/audio_engine.h"
#include "audio/audio_music.h"
#include "audio/audio_music_player.h"
#include "editor/scene_serializer.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"

#include "test_helpers.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Vestige;

namespace
{
const std::string kFixture =
    std::string(VESTIGE_AUDIO_FIXTURES_DIR) + "/music_loop_test.ogg";

constexpr float kEps = 1e-5f;
// MusicStreamState::minSecondsBuffered default — the planner's keep-ahead.
constexpr float kMinBuffered = 0.30f;
} // namespace

class AudioMusicPlayerTest : public ::testing::Test
{
protected:
    // No initialize() → headless (isAvailable() == false).
    AudioEngine engine;
    AudioMusicPlayer player{engine};
};

// Row 1 — load opens the decoder, seeds the full free-buffer ring, buffers 0 s.
TEST_F(AudioMusicPlayerTest, LoadLayerOpensDecoderAndSeedsRing_Row1)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    EXPECT_TRUE(player.isLayerLoaded(MusicLayer::Ambient));
    EXPECT_FALSE(player.isLayerPlaying(MusicLayer::Ambient));
    EXPECT_FLOAT_EQ(player.getLayerBufferedSeconds(MusicLayer::Ambient), 0.0f);
    EXPECT_EQ(player.getLayerFreeBufferCount(MusicLayer::Ambient),
              kBuffersPerLayer);
}

// Row 2 — after play + a tick, buffered audio reaches the keep-ahead target.
TEST_F(AudioMusicPlayerTest, PlayThenUpdateReachesKeepAhead_Row2)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    EXPECT_TRUE(player.isLayerPlaying(MusicLayer::Ambient));
    player.update(1.0f);
    EXPECT_GE(player.getLayerBufferedSeconds(MusicLayer::Ambient), kMinBuffered);
}

// Row 3 — fade-in: 0 → 1 over exactly 2 s at fadeSpeedPerSecond = 0.5/s.
TEST_F(AudioMusicPlayerTest, FadeInReachesUnity_Row3)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    player.setLayerTargetGain(MusicLayer::Ambient, 1.0f);
    player.update(2.0f);
    EXPECT_NEAR(player.getLayerGain(MusicLayer::Ambient), 1.0f, kEps);
}

// Row 4 — fade-out: 1 → 0; 2.1 s clears the at-the-boundary timing flake.
TEST_F(AudioMusicPlayerTest, FadeOutReachesZero_Row4)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    player.setLayerTargetGain(MusicLayer::Ambient, 1.0f);
    player.update(2.0f);
    ASSERT_NEAR(player.getLayerGain(MusicLayer::Ambient), 1.0f, kEps);
    player.setLayerTargetGain(MusicLayer::Ambient, 0.0f);
    player.update(2.1f);
    EXPECT_NEAR(player.getLayerGain(MusicLayer::Ambient), 0.0f, kEps);
}

// Row 5 — looping fixture over 2× track length loops at least once, never ends.
TEST_F(AudioMusicPlayerTest, LoopsOverTwiceTrackLength_Row5)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    for (int i = 0; i < 20; ++i)  // 20 × 0.05 s = 1.0 s = 2× the 0.5 s fixture.
    {
        player.update(0.05f);
    }
    const MusicStreamState& s = player.getLayerStreamState(MusicLayer::Ambient);
    EXPECT_GE(s.loopCount, 1u);
    EXPECT_FALSE(s.finished);
}

// Row 6 — clearAllLayers stops + unloads every layer.
TEST_F(AudioMusicPlayerTest, ClearAllLayersStopsAndUnloads_Row6)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    player.update(0.1f);
    player.clearAllLayers();
    EXPECT_FALSE(player.isLayerPlaying(MusicLayer::Ambient));
    EXPECT_FALSE(player.isLayerLoaded(MusicLayer::Ambient));
    EXPECT_EQ(player.getActiveLayerCount(), 0u);
}

// Row 7 — an enqueued stinger drains through update() (routed to the one-shot
// path). The concrete AudioEngine isn't a mockable interface, so we observe the
// queue draining rather than spying on playSound2D (design-reconcile note).
TEST_F(AudioMusicPlayerTest, StingerDrainsThroughUpdate_Row7)
{
    MusicStinger stinger;
    stinger.clipPath = kFixture;
    stinger.delaySeconds = 0.5f;
    stinger.volume = 1.0f;
    player.enqueueStinger(stinger);
    EXPECT_EQ(player.getPendingStingerCount(), 1u);
    player.update(0.6f);  // past the 0.5 s delay
    EXPECT_EQ(player.getPendingStingerCount(), 0u);
}

// Row 9 — a path escaping the sandbox roots is rejected; no decoder opened.
TEST_F(AudioMusicPlayerTest, SandboxRejectReturnsFalse_Row9)
{
    engine.setSandboxRoots({std::filesystem::path(VESTIGE_AUDIO_FIXTURES_DIR)});
    EXPECT_FALSE(player.loadLayer(MusicLayer::Ambient, "../../../etc/passwd"));
    EXPECT_FALSE(player.isLayerLoaded(MusicLayer::Ambient));
}

// Row 11 — update(0.0) is a true no-op (no slew, no buffer change).
TEST_F(AudioMusicPlayerTest, ZeroDtIsNoOp_Row11)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    player.setLayerTargetGain(MusicLayer::Ambient, 1.0f);
    player.update(0.5f);
    const float gain0 = player.getLayerGain(MusicLayer::Ambient);
    const float buf0 = player.getLayerBufferedSeconds(MusicLayer::Ambient);
    player.update(0.0f);
    EXPECT_FLOAT_EQ(player.getLayerGain(MusicLayer::Ambient), gain0);
    EXPECT_FLOAT_EQ(player.getLayerBufferedSeconds(MusicLayer::Ambient), buf0);
}

// Row 12 — reloading a layer resets the stream + ring and stops playback.
TEST_F(AudioMusicPlayerTest, ReloadResetsStreamAndRing_Row12)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    player.update(0.3f);
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));  // reload
    EXPECT_TRUE(player.isLayerLoaded(MusicLayer::Ambient));
    EXPECT_FALSE(player.isLayerPlaying(MusicLayer::Ambient));
    EXPECT_FLOAT_EQ(player.getLayerBufferedSeconds(MusicLayer::Ambient), 0.0f);
    EXPECT_EQ(player.getLayerFreeBufferCount(MusicLayer::Ambient),
              kBuffersPerLayer);
}

// Row 13 — loop boundary: crossing EOF rewinds cleanly, clears the transient
// EOF flag, keeps the sticky decoded-once flag, and playback continues.
// (Reworked from the design's per-tick choreography — the per-tick refill
// loop tops the ring up in one update(), so the seam is crossed inside a tick;
// see design-reconcile #2. We pin the observable post-conditions instead.)
TEST_F(AudioMusicPlayerTest, LoopBoundaryRewindsCleanly_Row13)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    for (int i = 0; i < 20; ++i)
    {
        player.update(0.05f);
    }
    const MusicStreamState& s = player.getLayerStreamState(MusicLayer::Ambient);
    EXPECT_GE(s.loopCount, 1u);
    EXPECT_FALSE(s.finished);
    EXPECT_TRUE(s.trackFullyDecodedOnce);
    // The rewind clears the transient flag; it must not be stuck true.
    EXPECT_FALSE(player.getLayerDecoderAtEof(MusicLayer::Ambient));
    EXPECT_GT(player.getLayerBufferedSeconds(MusicLayer::Ambient), 0.0f);
}

// Row 14 — negative dt is a true no-op, same as row 11.
TEST_F(AudioMusicPlayerTest, NegativeDtIsNoOp_Row14)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);
    player.setLayerTargetGain(MusicLayer::Ambient, 1.0f);
    player.update(0.5f);
    const float gain0 = player.getLayerGain(MusicLayer::Ambient);
    const float buf0 = player.getLayerBufferedSeconds(MusicLayer::Ambient);
    player.update(-1.0f);
    EXPECT_FLOAT_EQ(player.getLayerGain(MusicLayer::Ambient), gain0);
    EXPECT_FLOAT_EQ(player.getLayerBufferedSeconds(MusicLayer::Ambient), buf0);
}

// Edge — priming stops short of the track length, so no EOF is marked yet.
TEST_F(AudioMusicPlayerTest, PrimingShortOfTrackDoesNotMarkEof_Edge)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.playLayer(MusicLayer::Ambient);  // primes ~0.34 s < 0.5 s track.
    const MusicStreamState& s = player.getLayerStreamState(MusicLayer::Ambient);
    EXPECT_FALSE(s.trackFullyDecodedOnce);
    EXPECT_EQ(s.loopCount, 0u);
}

// Edge — a non-looping layer (loopAll=false → maxLoops=0) finishes + stops.
TEST_F(AudioMusicPlayerTest, NonLoopingTrackFinishesAndStops_Edge)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    player.setLayerLooping(MusicLayer::Ambient, false);
    player.playLayer(MusicLayer::Ambient);
    for (int i = 0; i < 40 && player.isLayerPlaying(MusicLayer::Ambient); ++i)
    {
        player.update(0.05f);
    }
    EXPECT_FALSE(player.isLayerPlaying(MusicLayer::Ambient));
    EXPECT_TRUE(player.getLayerStreamState(MusicLayer::Ambient).finished);
}

// Edge — applyIntensity drives loaded layers' target gains from one signal.
TEST_F(AudioMusicPlayerTest, ApplyIntensitySetsLayerTargets_Edge)
{
    ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
    ASSERT_TRUE(player.loadLayer(MusicLayer::Combat, kFixture));
    player.applyIntensity(0.0f);  // calm → ambient peaks, combat at 0.
    EXPECT_GT(player.getLayerTargetGain(MusicLayer::Ambient),
              player.getLayerTargetGain(MusicLayer::Combat));
    player.applyIntensity(0.75f);  // Combat's triangular peak.
    EXPECT_GT(player.getLayerTargetGain(MusicLayer::Combat),
              player.getLayerTargetGain(MusicLayer::Ambient));
}

// Step 1 — decode-rate smoke + bench. Confirms the fixture decodes and reports
// µs per 4096-frame chunk so the spec §6 budget can be re-pinned. Not a hard
// perf gate (CI hardware varies); the only hard assertion is that decode
// produced audio. The measured number is logged for the design's step-1 check.
TEST(AudioMusicPlayerDecodeBench, ChunkDecodeSmokeAndTiming_Step1)
{
    AudioEngine engine;  // headless
    constexpr int kRuns = 50;
    constexpr int kChunksPerPrime = 4;  // ~0.34 s / 0.085 s-per-chunk.
    std::chrono::steady_clock::duration decodeTime{};
    int totalChunksPrimed = 0;
    for (int run = 0; run < kRuns; ++run)
    {
        AudioMusicPlayer player{engine};
        // loadLayer (file open) is untimed; time only the decode prime.
        ASSERT_TRUE(player.loadLayer(MusicLayer::Ambient, kFixture));
        const auto t0 = std::chrono::steady_clock::now();
        player.playLayer(MusicLayer::Ambient);  // primes ~4 chunks of decode.
        decodeTime += std::chrono::steady_clock::now() - t0;
        ASSERT_GT(player.getLayerBufferedSeconds(MusicLayer::Ambient), 0.0f);
        totalChunksPrimed += kChunksPerPrime;
    }
    const double us =
        std::chrono::duration<double, std::micro>(decodeTime).count();
    const double usPerChunk = us / std::max(1, totalChunksPrimed);
    // NB: the default build is ASan+UBSan-instrumented (~3-5× decode
    // inflation), so this records the instrumented figure; divide by the
    // sanitiser overhead for the release estimate. Logged for the spec §6
    // re-pin, not asserted against the budget.
    RecordProperty("us_per_4096_frame_chunk_asan",
                   static_cast<int>(usPerChunk + 0.5));
    // Loose ceiling only — the design re-pins the real number from this log.
    EXPECT_LT(usPerChunk, 50000.0);  // 50 ms/chunk would mean something is wrong.
}

// ---------------------------------------------------------------------------
// Scene-serializer `music` block (rows 8, 10 + the v1→v2 backwards-compat
// load). Round-trips run headless — ResourceManager + an entity-light Scene
// need no GL context (same precedent as test_scene_serializer.cpp).
// ---------------------------------------------------------------------------

namespace fs = std::filesystem;

class MusicSceneSerializerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_dir = fs::temp_directory_path()
              / ("vestige_test_music_scene_" + Testing::vestigeTestStamp());
        fs::create_directories(m_dir);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }
    fs::path m_dir;
};

// Row 8 — a scene's music block round-trips: 3 layers + loopAll preserved.
TEST_F(MusicSceneSerializerTest, MusicBlockRoundTrips_Row8)
{
    Scene scene("MusicScene");
    ResourceManager resources;
    MusicSceneSettings musicOut;
    musicOut.loopAll = true;
    musicOut.layers = {{MusicLayer::Ambient, "assets/music/amb.ogg"},
                       {MusicLayer::Tension, "assets/music/tns.ogg"},
                       {MusicLayer::Combat, "assets/music/cbt.ogg"}};

    const fs::path path = m_dir / "music_roundtrip.scene";
    auto saved = SceneSerializer::saveScene(scene, path, resources, nullptr,
                                            nullptr, &musicOut);
    ASSERT_TRUE(saved.success) << saved.errorMessage;

    Scene reloaded("reloaded");
    MusicSceneSettings musicIn;
    auto loaded = SceneSerializer::loadScene(reloaded, path, resources, nullptr,
                                             nullptr, &musicIn);
    ASSERT_TRUE(loaded.success) << loaded.errorMessage;
    EXPECT_TRUE(musicIn.loopAll);
    ASSERT_EQ(musicIn.layers.size(), 3u);
    EXPECT_EQ(musicIn.layers[0].layer, MusicLayer::Ambient);
    EXPECT_EQ(musicIn.layers[0].clipPath, "assets/music/amb.ogg");
    EXPECT_EQ(musicIn.layers[1].layer, MusicLayer::Tension);
    EXPECT_EQ(musicIn.layers[2].layer, MusicLayer::Combat);
}

// Row 10 — an unknown layer label warns + is skipped; valid layers still load.
TEST_F(MusicSceneSerializerTest, UnknownLayerLabelWarnsAndSkips_Row10)
{
    nlohmann::json j;
    j["vestige_scene"]["format_version"] = SceneSerializer::CURRENT_FORMAT_VERSION;
    j["vestige_scene"]["name"] = "BogusLayerScene";
    j["entities"] = nlohmann::json::array();
    j["music"]["loopAll"] = true;
    j["music"]["layers"] = nlohmann::json::array(
        {{{"layer", "Bogus"}, {"clip", "x.ogg"}},
         {{"layer", "Combat"}, {"clip", "cbt.ogg"}}});

    const fs::path path = m_dir / "bogus_layer.scene";
    std::ofstream(path) << j.dump(4);

    Scene scene("s");
    ResourceManager resources;
    MusicSceneSettings music;
    auto loaded = SceneSerializer::loadScene(scene, path, resources, nullptr,
                                             nullptr, &music);
    ASSERT_TRUE(loaded.success) << loaded.errorMessage;
    EXPECT_GE(loaded.warningCount, 1);
    ASSERT_EQ(music.layers.size(), 1u);  // Bogus skipped, Combat kept.
    EXPECT_EQ(music.layers[0].layer, MusicLayer::Combat);
}

// v1→v2 — a committed pre-v2 scene (no music block) loads green with empty
// MusicSceneSettings (backwards-compatible read).
TEST_F(MusicSceneSerializerTest, PreV2SceneLoadsWithEmptyMusic)
{
    const fs::path path =
        fs::path(VESTIGE_SCENE_FIXTURES_DIR) / "pre_v2_no_music.scene";
    ASSERT_TRUE(fs::exists(path)) << path.string();

    Scene scene("s");
    ResourceManager resources;
    MusicSceneSettings music;
    auto loaded = SceneSerializer::loadScene(scene, path, resources, nullptr,
                                             nullptr, &music);
    ASSERT_TRUE(loaded.success) << loaded.errorMessage;
    EXPECT_TRUE(music.layers.empty());
}
