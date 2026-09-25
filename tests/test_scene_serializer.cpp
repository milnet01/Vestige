// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scene_serializer.cpp
/// @brief Unit tests for scene save/load round-trip serialization.
#include "editor/scene_serializer.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "renderer/material.h"
#include "utils/entity_serializer.h"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

#include "test_helpers.h"

using namespace Vestige;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture — no GPU context needed (serialization is pure data)
// ---------------------------------------------------------------------------

class SceneSerializerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Unique per-process: ctest -j runs each TEST_F as a separate
        // process; a shared path let one process's TearDown remove_all
        // another process's in-flight files.
        m_testDir = fs::temp_directory_path()
                  / ("vestige_test_scenes_" + Testing::vestigeTestStamp());
        fs::create_directories(m_testDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
    }

    fs::path m_testDir;
};

// ---------------------------------------------------------------------------
// Scene metadata
// ---------------------------------------------------------------------------

TEST_F(SceneSerializerTest, ReadMetadataFromEmptyFile)
{
    // Non-existent file returns default metadata
    SceneMetadata meta = SceneSerializer::readMetadata(m_testDir / "nonexistent.scene");
    EXPECT_EQ(meta.formatVersion, 0);
    EXPECT_TRUE(meta.name.empty());
}

TEST_F(SceneSerializerTest, ReadMetadataFromValidFile)
{
    // Write a minimal valid scene file
    nlohmann::json j;
    j["vestige_scene"]["format_version"] = 1;
    j["vestige_scene"]["name"] = "Test Scene";
    j["vestige_scene"]["engine_version"] = "0.5.0";
    j["vestige_scene"]["created"] = "2026-03-20T00:00:00Z";
    j["entities"] = nlohmann::json::array();

    fs::path path = m_testDir / "meta_test.scene";
    std::ofstream out(path);
    out << j.dump(4);
    out.close();

    SceneMetadata meta = SceneSerializer::readMetadata(path);
    EXPECT_EQ(meta.formatVersion, 1);
    EXPECT_EQ(meta.name, "Test Scene");
    EXPECT_EQ(meta.engineVersion, "0.5.0");
    EXPECT_EQ(meta.created, "2026-03-20T00:00:00Z");
}

// 3D_E-0683: the stamp written into every saved scene is the project version
// of the build that wrote it, read from the top-level CMakeLists.txt, which
// the release recipe bumps. It read a hard-coded "0.5.0" the project had never
// been at.
TEST(SceneSerializerEngineVersion, MatchesTheCMakeProjectVersion)
{
    std::ifstream in(VESTIGE_ROOT_CMAKELISTS);
    ASSERT_TRUE(in.is_open()) << VESTIGE_ROOT_CMAKELISTS;
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    std::smatch m;
    ASSERT_TRUE(std::regex_search(
        text, m, std::regex(R"(project\s*\(\s*Vestige\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+))")));
    EXPECT_EQ(std::string(SceneSerializer::ENGINE_VERSION), m[1].str());

    // The root VERSION file is the release workflows' floor and must name the
    // same version; it had drifted to 0.1.60 while CMake said 0.1.70.
    std::ifstream versionFile(fs::path(VESTIGE_ROOT_CMAKELISTS).parent_path() / "VERSION");
    ASSERT_TRUE(versionFile.is_open());
    std::string version;
    std::getline(versionFile, version);
    EXPECT_EQ(version, m[1].str()) << "VERSION and CMakeLists.txt project(VERSION) disagree";
}

// Note: serializeToString and the full save/load round-trip require a
// ResourceManager (GL context) and are exercised by integration tests
// rather than this unit-test file.

// ---------------------------------------------------------------------------
// File structure validation
// ---------------------------------------------------------------------------

// Slice 18 Ts1 cleanup: renamed from `LoadRejectsMissingHeader` — the
// body only exercises `readMetadata`, which is the standalone
// header-parsing helper. `loadScene` itself needs GL and is exercised
// at engine launch. Same `*ShapeReferenceDocument`/`*DoesNotReject`
// naming precedent shipped with the Ed11 metadata pins.
TEST_F(SceneSerializerTest, ReadMetadataReturnsZeroVersionForMissingHeader)
{
    // Write a JSON file without the vestige_scene header
    nlohmann::json j;
    j["entities"] = nlohmann::json::array();

    fs::path path = m_testDir / "no_header.scene";
    std::ofstream out(path);
    out << j.dump(4);
    out.close();

    Scene scene("Test");
    // We need a ResourceManager for loadScene but can't create one without GPU.
    // Just test that the metadata validation rejects this file.
    SceneMetadata meta = SceneSerializer::readMetadata(path);
    EXPECT_EQ(meta.formatVersion, 0);  // Missing header → version 0
}

TEST_F(SceneSerializerTest, ReadMetadataEchoesRawFormatVersion)
{
    // readMetadata() is a thin parser; it does not gate on format version.
    // The actual rejection of a future format happens later in loadScene
    // (which requires a GL context and is covered by integration tests).
    nlohmann::json j;
    j["vestige_scene"]["format_version"] = 999;
    j["vestige_scene"]["name"] = "Future";
    j["entities"] = nlohmann::json::array();

    fs::path path = m_testDir / "future_version.scene";
    std::ofstream out(path);
    out << j.dump(4);
    out.close();

    SceneMetadata meta = SceneSerializer::readMetadata(path);
    EXPECT_EQ(meta.formatVersion, 999);
}

TEST_F(SceneSerializerTest, LoadRejectsNonexistentFile)
{
    SceneMetadata meta = SceneSerializer::readMetadata(m_testDir / "does_not_exist.scene");
    EXPECT_EQ(meta.formatVersion, 0);
}

TEST_F(SceneSerializerTest, LoadRejectsInvalidJson)
{
    fs::path path = m_testDir / "invalid.scene";
    std::ofstream out(path);
    out << "not valid json {{{";
    out.close();

    SceneMetadata meta = SceneSerializer::readMetadata(path);
    EXPECT_EQ(meta.formatVersion, 0);
}

// ---------------------------------------------------------------------------
// Scene clearEntities
// ---------------------------------------------------------------------------

TEST_F(SceneSerializerTest, ClearEntitiesRemovesAllChildren)
{
    Scene scene("Test");
    scene.createEntity("A");
    scene.createEntity("B");
    scene.createEntity("C");

    EXPECT_EQ(scene.getRoot()->getChildren().size(), 3u);

    scene.clearEntities();
    EXPECT_EQ(scene.getRoot()->getChildren().size(), 0u);
}

TEST_F(SceneSerializerTest, SetNameWorks)
{
    Scene scene("Original");
    EXPECT_EQ(scene.getName(), "Original");

    scene.setName("Renamed");
    EXPECT_EQ(scene.getName(), "Renamed");
}

// Note: the engine's atomic-write path (write to .tmp + rename) is
// exercised in tests/test_atomic_write_routing.cpp against the real
// SceneSerializer save path. Tests that wrote via std::ofstream and
// asserted fs::exists() proved nothing about the production code path
// and have been removed.

// ---------------------------------------------------------------------------
// JSON structure of serialized entity data
// ---------------------------------------------------------------------------

// /test-audit 2026-05-17 Ts19-A3: the previous shape-reference tests asserted
// on a JSON value they had just constructed by hand (tautology — passes
// regardless of what the production serializer emits). These replacements
// drive `EntitySerializer::serializeEntity` directly and assert the same
// shape against the *production* output, so a field rename in the serializer
// without a matching update here will now fail.
TEST_F(SceneSerializerTest, EntityJsonShapeMatchesProductionSerializer)
{
    Scene scene("ShapeTest");
    Entity* e = scene.createEntity("TestEntity");
    e->transform.position = glm::vec3(1.0f, 2.0f, 3.0f);
    e->transform.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
    e->transform.scale    = glm::vec3(1.0f, 1.0f, 1.0f);
    e->setVisible(true);
    e->setLocked(false);

    ResourceManager resources;
    nlohmann::json entityJson = EntitySerializer::serializeEntity(*e, resources);

    EXPECT_EQ(entityJson["name"], "TestEntity");
    ASSERT_TRUE(entityJson.contains("transform"));
    EXPECT_FLOAT_EQ(entityJson["transform"]["position"][0].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(entityJson["transform"]["position"][1].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(entityJson["transform"]["position"][2].get<float>(), 3.0f);
    EXPECT_FLOAT_EQ(entityJson["transform"]["rotation"][1].get<float>(), 90.0f);
    EXPECT_TRUE(entityJson["visible"].get<bool>());
    EXPECT_FALSE(entityJson["locked"].get<bool>());
}

TEST_F(SceneSerializerTest, SceneEnvelopeRoundTripsThroughSerializer)
{
    Scene sceneOut("MyScene");
    Entity* cube = sceneOut.createEntity("Cube");
    cube->transform.position = glm::vec3(0.0f);

    ResourceManager resources;
    fs::path scenePath = m_testDir / "envelope_roundtrip.scene";
    auto saveResult = SceneSerializer::saveScene(sceneOut, scenePath.string(), resources);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    // Read the raw JSON back and check the production envelope shape.
    std::ifstream f(scenePath);
    ASSERT_TRUE(f.good());
    nlohmann::json sceneJson;
    f >> sceneJson;

    ASSERT_TRUE(sceneJson.contains("vestige_scene"));
    ASSERT_TRUE(sceneJson.contains("entities"));
    EXPECT_EQ(sceneJson["vestige_scene"]["format_version"],
              SceneSerializer::CURRENT_FORMAT_VERSION);
    EXPECT_EQ(sceneJson["entities"].size(), 1u);
    EXPECT_EQ(sceneJson["entities"][0]["name"], "Cube");
}
