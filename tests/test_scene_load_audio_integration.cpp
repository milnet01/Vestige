// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scene_load_audio_integration.cpp
/// @brief Phase 10.9 Slice 8 W8 (part 2/2) — scene-load → music integration.
///
/// Both the engine start-up scene load (`engine/core/engine.cpp`) and the
/// editor Open / Recent / autosave-recover loads (`engine/editor/file_menu.cpp`)
/// funnel through the same two calls: `SceneSerializer::loadScene(... , &music)`
/// then `applyMusicSceneSettings(player, music)`. That shared flow is what these
/// tests pin. The full Engine / FileMenu GL paths (window + ImGui) are verified
/// at app launch, same precedent as other GL-bound paths in this suite.
///
/// Runs headless: the AudioEngine is never `initialize()`d (no device), but
/// stb_vorbis decode is pure file IO, so loadLayer / playLayer fully exercise.

#include <gtest/gtest.h>

#include "audio/audio_engine.h"
#include "audio/audio_music.h"
#include "audio/audio_music_player.h"
#include "editor/scene_serializer.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"

#include "test_helpers.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace Vestige;

namespace
{
namespace fs = std::filesystem;
const std::string kClip =
    std::string(VESTIGE_AUDIO_FIXTURES_DIR) + "/music_loop_test.ogg";
} // namespace

class SceneLoadAudioIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_dir = fs::temp_directory_path()
              / ("vestige_test_scene_audio_" + Testing::vestigeTestStamp());
        fs::create_directories(m_dir);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }
    fs::path m_dir;
};

// A scene whose `music` block names two layers loads + plays both through the
// player via the exact serializer→helper flow both call sites use.
TEST_F(SceneLoadAudioIntegrationTest, SceneMusicBlockLoadsAndPlaysThroughPlayer)
{
    nlohmann::json j;
    j["vestige_scene"]["format_version"] = SceneSerializer::CURRENT_FORMAT_VERSION;
    j["vestige_scene"]["name"] = "integ";
    j["entities"] = nlohmann::json::array();
    j["music"]["loopAll"] = true;
    j["music"]["layers"] = nlohmann::json::array(
        {{{"layer", "Ambient"}, {"clip", kClip}},
         {{"layer", "Combat"}, {"clip", kClip}}});

    const fs::path path = m_dir / "integ.scene";
    std::ofstream(path) << j.dump(4);

    Scene scene("s");
    ResourceManager resources;
    MusicSceneSettings music;
    auto loaded = SceneSerializer::loadScene(scene, path, resources, nullptr,
                                             nullptr, &music);
    ASSERT_TRUE(loaded.success) << loaded.errorMessage;
    ASSERT_EQ(music.layers.size(), 2u);

    AudioEngine engine;  // headless
    AudioMusicPlayer player{engine};
    const int warnings = applyMusicSceneSettings(player, music);

    EXPECT_EQ(warnings, 0);
    EXPECT_TRUE(player.isLayerLoaded(MusicLayer::Ambient));
    EXPECT_TRUE(player.isLayerPlaying(MusicLayer::Ambient));
    EXPECT_TRUE(player.isLayerPlaying(MusicLayer::Combat));
    // loopAll=true → infinite loop policy on each layer.
    EXPECT_EQ(player.getLayerStreamState(MusicLayer::Ambient).maxLoops, -1);
}

// Loading a second scene replaces the previous scene's music (the scene-swap
// contract both call sites depend on — applyMusicSceneSettings clears first).
TEST_F(SceneLoadAudioIntegrationTest, LoadingASecondSceneReplacesPreviousMusic)
{
    AudioEngine engine;
    AudioMusicPlayer player{engine};

    MusicSceneSettings sceneA;
    sceneA.layers = {{MusicLayer::Ambient, kClip}};
    applyMusicSceneSettings(player, sceneA);
    ASSERT_TRUE(player.isLayerPlaying(MusicLayer::Ambient));

    MusicSceneSettings sceneB;
    sceneB.layers = {{MusicLayer::Combat, kClip}};
    applyMusicSceneSettings(player, sceneB);

    EXPECT_FALSE(player.isLayerLoaded(MusicLayer::Ambient));  // cleared
    EXPECT_TRUE(player.isLayerPlaying(MusicLayer::Combat));
}

// loopAll=false propagates the one-shot policy to each layer's stream.
TEST_F(SceneLoadAudioIntegrationTest, LoopAllFalseSetsOneShotPolicy)
{
    AudioEngine engine;
    AudioMusicPlayer player{engine};

    MusicSceneSettings music;
    music.loopAll = false;
    music.layers = {{MusicLayer::Ambient, kClip}};
    applyMusicSceneSettings(player, music);

    EXPECT_EQ(player.getLayerStreamState(MusicLayer::Ambient).maxLoops, 0);
}

// An empty music block (e.g. a v1 scene) clears music and loads no layers.
TEST_F(SceneLoadAudioIntegrationTest, EmptyMusicSettingsClearsAndLoadsNothing)
{
    AudioEngine engine;
    AudioMusicPlayer player{engine};

    MusicSceneSettings seeded;
    seeded.layers = {{MusicLayer::Ambient, kClip}};
    applyMusicSceneSettings(player, seeded);
    ASSERT_TRUE(player.isLayerLoaded(MusicLayer::Ambient));

    const int warnings = applyMusicSceneSettings(player, MusicSceneSettings{});
    EXPECT_EQ(warnings, 0);
    EXPECT_FALSE(player.isLayerLoaded(MusicLayer::Ambient));
    EXPECT_EQ(player.getActiveLayerCount(), 0u);
}
