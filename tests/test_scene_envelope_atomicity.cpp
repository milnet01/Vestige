// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scene_envelope_atomicity.cpp
/// @brief Phase 10.9 Slice 12 Ed11 — scene-envelope atomicity contract.
///
/// Ed11 collapses the four-step save sequence (write scene.json, re-read,
/// inject env+terrain, re-write scene.json; write heightmap; write
/// splatmap) into a single manifest-backed atomic commit. The scene.json
/// embeds `terrain.heightmap_file` / `terrain.splatmap_file` keys naming
/// the current epoch's side-files; the scene.json atomic write is the
/// commit point; orphans from prior epochs (and pre-Ed11 unsuffixed
/// legacy names) are swept after the commit succeeds.
///
/// The save path needs a Terrain object backed by GL textures, so the
/// full round-trip lives in the launchable engine. Here we pin the
/// GC contract directly via the public
/// `SceneSerializer::garbageCollectEpochFiles` helper and pin the
/// manifest-aware JSON shape — the two pieces a unit-test process can
/// exercise without a GL context.

#include "editor/scene_serializer.h"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

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

using namespace Vestige;
namespace fs = std::filesystem;

namespace
{

class SceneEnvelopeAtomicityTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Per-process + per-test stamping so `ctest -j` runs of this
        // fixture don't race each other's TearDown.
        m_dir = fs::temp_directory_path()
              / ("vestige_ed11_envelope_"
                  + std::to_string(VESTIGE_GETPID()) + "_"
                  + ::testing::UnitTest::GetInstance()
                      ->current_test_info()->name());
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    // Plants a non-empty file at `m_dir / name` so the GC step has
    // something to delete.
    void touch(const std::string& name, const std::string& body = "x")
    {
        std::ofstream out(m_dir / name);
        out << body;
    }

    bool exists(const std::string& name) const
    {
        return fs::exists(m_dir / name);
    }

    fs::path m_dir;
};

} // namespace

// ---------------------------------------------------------------------------
// garbageCollectEpochFiles — keeps the named pair, sweeps everything else
// that matches the stem's heightmap/splatmap pattern.
// ---------------------------------------------------------------------------

TEST_F(SceneEnvelopeAtomicityTest, GcRemovesStaleEpochHeightmaps_Ed11)
{
    // The "current" pair we'd keep across the commit.
    const std::string keepH = "myScene.heightmap.1000-0.r32";
    const std::string keepS = "myScene.splatmap.1000-0.splat";
    touch(keepH);
    touch(keepS);

    // Stale epoch-suffixed heightmaps from earlier saves.
    touch("myScene.heightmap.500-0.r32");
    touch("myScene.heightmap.500-1.r32");
    touch("myScene.splatmap.500-0.splat");

    SceneSerializer::garbageCollectEpochFiles(
        m_dir, "myScene",
        m_dir / keepH, m_dir / keepS);

    EXPECT_TRUE(exists(keepH))
        << "GC must keep the currently-named heightmap";
    EXPECT_TRUE(exists(keepS))
        << "GC must keep the currently-named splatmap";
    EXPECT_FALSE(exists("myScene.heightmap.500-0.r32"));
    EXPECT_FALSE(exists("myScene.heightmap.500-1.r32"));
    EXPECT_FALSE(exists("myScene.splatmap.500-0.splat"));
}

TEST_F(SceneEnvelopeAtomicityTest, GcRemovesLegacyUnsuffixedNames_Ed11)
{
    // Pre-Ed11 save layout: heightmap/splatmap with no epoch in the
    // filename. After a successful Ed11 save, the GC step must sweep
    // these so the directory has only the new epoch's pair.
    const std::string keepH = "myScene.heightmap.2000-0.r32";
    const std::string keepS = "myScene.splatmap.2000-0.splat";
    touch(keepH);
    touch(keepS);
    touch("myScene.heightmap.r32");
    touch("myScene.splatmap.splat");

    SceneSerializer::garbageCollectEpochFiles(
        m_dir, "myScene",
        m_dir / keepH, m_dir / keepS);

    EXPECT_TRUE(exists(keepH));
    EXPECT_TRUE(exists(keepS));
    EXPECT_FALSE(exists("myScene.heightmap.r32"))
        << "GC must remove the legacy unsuffixed heightmap";
    EXPECT_FALSE(exists("myScene.splatmap.splat"))
        << "GC must remove the legacy unsuffixed splatmap";
}

TEST_F(SceneEnvelopeAtomicityTest, GcLeavesUnrelatedStemsAlone_Ed11)
{
    // Two scenes coexisting in the same directory must not GC each
    // other's side-files. Only files prefixed with the active stem
    // are candidates.
    const std::string keepH = "sceneA.heightmap.3000-0.r32";
    const std::string keepS = "sceneA.splatmap.3000-0.splat";
    touch(keepH);
    touch(keepS);

    // Different scene's files — must survive.
    touch("sceneB.heightmap.500-0.r32");
    touch("sceneB.splatmap.500-0.splat");
    touch("sceneB.heightmap.r32");

    // A scene.json sitting alongside must also survive.
    touch("sceneA.scene", "{}");

    SceneSerializer::garbageCollectEpochFiles(
        m_dir, "sceneA",
        m_dir / keepH, m_dir / keepS);

    EXPECT_TRUE(exists(keepH));
    EXPECT_TRUE(exists(keepS));
    EXPECT_TRUE(exists("sceneB.heightmap.500-0.r32"))
        << "GC must not touch a different scene's files";
    EXPECT_TRUE(exists("sceneB.splatmap.500-0.splat"));
    EXPECT_TRUE(exists("sceneB.heightmap.r32"));
    EXPECT_TRUE(exists("sceneA.scene"))
        << "GC must not touch the scene.json itself";
}

TEST_F(SceneEnvelopeAtomicityTest, GcOnEmptyDirIsNoOp_Ed11)
{
    // No keep-pair on disk yet, no stale files either — GC must just
    // return without throwing or creating anything.
    SceneSerializer::garbageCollectEpochFiles(
        m_dir, "fresh",
        m_dir / "fresh.heightmap.0-0.r32",
        m_dir / "fresh.splatmap.0-0.splat");

    std::error_code ec;
    auto count = std::distance(fs::directory_iterator(m_dir, ec),
                               fs::directory_iterator{});
    EXPECT_EQ(count, 0);
}

TEST_F(SceneEnvelopeAtomicityTest, GcOnMissingDirIsNoOp_Ed11)
{
    // A typo'd or pre-creation directory path must not throw — GC
    // failures are non-fatal.
    const fs::path missing = m_dir / "does_not_exist";
    SceneSerializer::garbageCollectEpochFiles(
        missing, "fresh",
        missing / "fresh.heightmap.0-0.r32",
        missing / "fresh.splatmap.0-0.splat");

    EXPECT_FALSE(fs::exists(missing));
}

TEST_F(SceneEnvelopeAtomicityTest, GcKeepsOnlyTheNamedPair_Ed11)
{
    // Multi-epoch dogpile — five prior saves all left side-files
    // behind because the user crashed the GC step each time. The
    // next clean save must collapse the dir to exactly the keep-pair.
    touch("worldStem.heightmap.10-0.r32");
    touch("worldStem.heightmap.20-0.r32");
    touch("worldStem.heightmap.30-0.r32");
    touch("worldStem.heightmap.40-0.r32");
    touch("worldStem.splatmap.10-0.splat");
    touch("worldStem.splatmap.20-0.splat");
    touch("worldStem.splatmap.30-0.splat");
    touch("worldStem.splatmap.40-0.splat");
    touch("worldStem.heightmap.r32");
    touch("worldStem.splatmap.splat");

    const std::string keepH = "worldStem.heightmap.50-0.r32";
    const std::string keepS = "worldStem.splatmap.50-0.splat";
    touch(keepH);
    touch(keepS);

    SceneSerializer::garbageCollectEpochFiles(
        m_dir, "worldStem",
        m_dir / keepH, m_dir / keepS);

    // Count surviving heightmap/splatmap files — must be exactly two.
    int heightmapCount = 0;
    int splatmapCount  = 0;
    for (const auto& entry : fs::directory_iterator(m_dir))
    {
        const std::string name = entry.path().filename().string();
        if (name.find("worldStem.heightmap.") == 0) heightmapCount++;
        if (name.find("worldStem.splatmap.")  == 0) splatmapCount++;
    }
    EXPECT_EQ(heightmapCount, 1);
    EXPECT_EQ(splatmapCount, 1);
    EXPECT_TRUE(exists(keepH));
    EXPECT_TRUE(exists(keepS));
}

// ---------------------------------------------------------------------------
// Manifest-aware load path: readMetadata over a scene.json carrying the
// new terrain.heightmap_file / splatmap_file keys must still parse the
// envelope (no schema regression). The full Terrain load requires GL and
// is exercised at engine launch.
// ---------------------------------------------------------------------------

TEST_F(SceneEnvelopeAtomicityTest, ReadMetadataAcceptsManifestKeys_Ed11)
{
    nlohmann::json j;
    j["vestige_scene"]["format_version"] = 1;
    j["vestige_scene"]["name"] = "WithManifest";
    j["vestige_scene"]["engine_version"] = "0.5.0";
    j["vestige_scene"]["created"] = "2026-05-16T00:00:00Z";
    j["entities"] = nlohmann::json::array();
    j["terrain"]["width"] = 257;
    j["terrain"]["depth"] = 257;
    j["terrain"]["heightmap_file"] = "WithManifest.heightmap.1747349123456-7.r32";
    j["terrain"]["splatmap_file"]  = "WithManifest.splatmap.1747349123456-7.splat";

    const fs::path p = m_dir / "WithManifest.scene";
    std::ofstream out(p);
    out << j.dump(4);
    out.close();

    SceneMetadata meta = SceneSerializer::readMetadata(p);
    EXPECT_EQ(meta.formatVersion, 1);
    EXPECT_EQ(meta.name, "WithManifest");
    // The metadata reader doesn't surface terrain fields directly, but
    // it must not bail out on the new keys either.
}

TEST_F(SceneEnvelopeAtomicityTest, ReadMetadataAcceptsLegacyTerrainShape_Ed11)
{
    // Pre-Ed11 terrain section had no heightmap_file / splatmap_file
    // entries. Backward-compat: such a scene must still parse, and
    // the load path will fall back to the legacy unsuffixed
    // <stem>.heightmap.r32 / <stem>.splatmap.splat names.
    nlohmann::json j;
    j["vestige_scene"]["format_version"] = 1;
    j["vestige_scene"]["name"] = "LegacyScene";
    j["vestige_scene"]["engine_version"] = "0.5.0";
    j["entities"] = nlohmann::json::array();
    j["terrain"]["width"] = 129;
    j["terrain"]["depth"] = 129;
    // No heightmap_file / splatmap_file — that's the pre-Ed11 shape.

    const fs::path p = m_dir / "LegacyScene.scene";
    std::ofstream out(p);
    out << j.dump(4);
    out.close();

    SceneMetadata meta = SceneSerializer::readMetadata(p);
    EXPECT_EQ(meta.formatVersion, 1);
    EXPECT_EQ(meta.name, "LegacyScene");
}
