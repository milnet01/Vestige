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
#include "utils/atomic_write.h"
#include "test_helpers.h"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
              / ("vestige_ed11_envelope_" + Testing::vestigeTestStamp());
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

// Slice 18 Ts1 cleanup: renamed from `GcOnEmptyDirIsNoOp_Ed11` — the
// body confirms GC against an empty dir doesn't throw, not that the
// GC code path itself observed an empty sweep. The "unrelated-stem
// files survive" branch is pinned by `GcKeepsOnlyTheNamedPair`.
TEST_F(SceneEnvelopeAtomicityTest, GcOnEmptyDirDoesNotThrow_Ed11)
{
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

    // Count surviving heightmap/splatmap files — must be exactly the
    // keep-pair. Extension-filter the loop so an unrelated artifact
    // (e.g. a stale `.bak`) can't satisfy the count by accident.
    int heightmapCount = 0;
    int splatmapCount  = 0;
    for (const auto& entry : fs::directory_iterator(m_dir))
    {
        const std::string name = entry.path().filename().string();
        const std::string ext  = entry.path().extension().string();
        if (name.find("worldStem.heightmap.") == 0 && ext == ".r32")   heightmapCount++;
        if (name.find("worldStem.splatmap.")  == 0 && ext == ".splat") splatmapCount++;
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

// SceneMetadata intentionally doesn't surface terrain fields — those
// live under the loadScene env-aware overload, which needs a GL context.
// What this test pins is the negative invariant: the metadata reader
// (used by recent-files / window-title / readMetadata callers that just
// want the scene name) must tolerate the Ed11 manifest keys without
// bailing. A regression here would surface as silently-empty metadata
// after an Ed11 save, which the engine launches over.
TEST_F(SceneEnvelopeAtomicityTest, ReadMetadataDoesNotRejectManifestKeys_Ed11)
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
}

// ---------------------------------------------------------------------------
// AtomicWrite rollback contract — the saveScene commit-point relies on
// this. Failure of `AtomicWrite::writeFile` must leave the target file
// (and the directory) byte-identical to its pre-call state. The
// SaveScene env-aware overload reduces to: "stage side-files; if all
// staged, atomic-write scene.json; on any failure, scene.json on disk
// stays at the old epoch because the failed writeFile preserved it."
// The literal early-return in saveScene needs no test (it's C++
// short-circuit). What does need testing is that writeFile honours the
// rollback contract — which the test hooks let us exercise without a
// real filesystem failure.
// ---------------------------------------------------------------------------

class AtomicWriteRollbackTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_dir = fs::temp_directory_path()
              / ("vestige_ed11_rollback_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir);
        m_target = m_dir / "scene.json";
    }

    void TearDown() override
    {
        AtomicWrite::TestHooks::clearForcedFailure();
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    std::string readAll(const fs::path& p) const
    {
        std::ifstream in(p);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    fs::path m_dir;
    fs::path m_target;
};

TEST_F(AtomicWriteRollbackTest, ForcedFailureReturnsTheArmedStatus_Ed11)
{
    AtomicWrite::TestHooks::forceNextWriteFailure(
        AtomicWrite::Status::RenameFailed);
    const auto status = AtomicWrite::writeFile(m_target, "new content");
    EXPECT_EQ(status, AtomicWrite::Status::RenameFailed);
}

TEST_F(AtomicWriteRollbackTest, ForcedFailureLeavesExistingTargetUntouched_Ed11)
{
    // Plant the old-epoch scene.json equivalent.
    {
        std::ofstream out(m_target);
        out << "OLD-EPOCH-CONTENT";
    }
    ASSERT_EQ(readAll(m_target), "OLD-EPOCH-CONTENT");

    AtomicWrite::TestHooks::forceNextWriteFailure(
        AtomicWrite::Status::TempWriteFailed);

    const auto status = AtomicWrite::writeFile(m_target, "NEW-EPOCH-CONTENT");
    ASSERT_NE(status, AtomicWrite::Status::Ok)
        << "hook must not return Ok";

    // The headline Ed11 invariant: on writeFile failure, the target
    // file is bit-identical to its pre-call state. saveScene's
    // "scene.json is the commit point" guarantee reduces to this.
    EXPECT_EQ(readAll(m_target), "OLD-EPOCH-CONTENT")
        << "forced AtomicWrite failure must not overwrite the target";
}

TEST_F(AtomicWriteRollbackTest, ForcedFailureLeavesNoTmpArtifact_Ed11)
{
    AtomicWrite::TestHooks::forceNextWriteFailure(
        AtomicWrite::Status::FsyncFailed);

    const auto status = AtomicWrite::writeFile(m_target, "doesn't matter");
    ASSERT_NE(status, AtomicWrite::Status::Ok);

    fs::path tmp = m_target;
    tmp += ".tmp";
    EXPECT_FALSE(fs::exists(m_target))
        << "forced failure must not create the target";
    EXPECT_FALSE(fs::exists(tmp))
        << "forced failure must not leave a .tmp sidecar";
}

TEST_F(AtomicWriteRollbackTest, FailureIsSingleShotAndAutoClears_Ed11)
{
    // Arm one failure, fire one failure, then a normal write succeeds
    // and the target reaches the new content.
    AtomicWrite::TestHooks::forceNextWriteFailure(
        AtomicWrite::Status::RenameFailed);
    EXPECT_NE(AtomicWrite::writeFile(m_target, "first"),
              AtomicWrite::Status::Ok);

    // Now writeFile must work normally — the hook is single-shot.
    EXPECT_EQ(AtomicWrite::writeFile(m_target, "second"),
              AtomicWrite::Status::Ok);
    EXPECT_EQ(readAll(m_target), "second");
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
