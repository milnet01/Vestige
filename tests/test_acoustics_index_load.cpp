// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_acoustics_index_load.cpp
/// @brief AX3 B4 — runtime baked-probe index loader. Covers `loadAcousticsIndex`:
///        parse + IR-path resolution against the `<stem>_acoustics/` sidecar dir,
///        graceful empties (no path / unbaked / malformed), path-separator
///        rejection, and that the shared nearest-probe lookup picks the closest
///        baked probe. Device-free — no engine, audio device, or physics.

#include "systems/reverb_system.h"

#include "audio/acoustic_baker.h"
#include "audio/acoustic_probe_component.h"
#include "scene/scene.h"

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Vestige;
namespace fs = std::filesystem;

namespace
{

/// Fresh, empty temp dir for one test.
fs::path uniqueDir(const char* leaf)
{
    const fs::path d = fs::temp_directory_path() / "vestige_ax3_b4" / leaf;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

/// Write `index` as `acoustics_index.json` under `<stem>_acoustics/` next to a
/// (never-created) scene.json at `sceneJsonPath`; return that scene path — the
/// exact string a loaded `Scene` reports via `getSourcePath()`.
std::string writeIndexBeside(const fs::path& sceneJsonPath, const nlohmann::json& index)
{
    const fs::path sidecar =
        sceneJsonPath.parent_path() / (sceneJsonPath.stem().string() + "_acoustics");
    fs::create_directories(sidecar);
    std::ofstream(sidecar / "acoustics_index.json", std::ios::binary) << index.dump(2);
    return sceneJsonPath.string();
}

} // namespace

TEST(AcousticsIndexLoad, ParsesProbesAndResolvesIrPaths)
{
    const fs::path dir   = uniqueDir("parse");
    const fs::path scene = dir / "temple.json";

    nlohmann::json idx;
    idx["version"]    = 1;
    idx["sampleRate"] = 48000;
    idx["probes"]     = nlohmann::json::array({
        { { "id", 1 }, { "position", { 1.0, 2.0, 3.0 } },
          { "influenceRadius", 5.0 }, { "ir", "probe_1.wav" } },
        { { "id", 2 }, { "position", { -4.0, 0.0, 0.5 } },
          { "influenceRadius", 8.0 }, { "ir", "probe_2.wav" } },
    });

    const auto probes = loadAcousticsIndex(writeIndexBeside(scene, idx));
    ASSERT_EQ(probes.size(), 2u);

    EXPECT_EQ(probes[0].id, 1u);
    EXPECT_FLOAT_EQ(probes[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(probes[0].position.y, 2.0f);
    EXPECT_FLOAT_EQ(probes[0].position.z, 3.0f);
    EXPECT_FLOAT_EQ(probes[0].influenceRadius, 5.0f);

    // The IR filename resolves against the `<stem>_acoustics/` sidecar dir.
    EXPECT_EQ(fs::path(probes[0].irPath), dir / "temple_acoustics" / "probe_1.wav");
    EXPECT_EQ(fs::path(probes[1].irPath), dir / "temple_acoustics" / "probe_2.wav");

    fs::remove_all(dir);
}

TEST(AcousticsIndexLoad, EmptyPathAndUnbakedSceneYieldNoProbes)
{
    EXPECT_TRUE(loadAcousticsIndex("").empty());  // in-memory scene, no file

    const fs::path dir = uniqueDir("unbaked");
    // A scene path with no `<stem>_acoustics/` sidecar → unbaked → empty so the
    // runtime falls back to authored zones.
    EXPECT_TRUE(loadAcousticsIndex((dir / "bare.json").string()).empty());
    fs::remove_all(dir);
}

TEST(AcousticsIndexLoad, MalformedIndexYieldsNoProbes)
{
    const fs::path dir     = uniqueDir("malformed");
    const fs::path sidecar = dir / "scene_acoustics";
    fs::create_directories(sidecar);
    std::ofstream(sidecar / "acoustics_index.json", std::ios::binary) << "{ this is not json ";

    EXPECT_TRUE(loadAcousticsIndex((dir / "scene.json").string()).empty());
    fs::remove_all(dir);
}

TEST(AcousticsIndexLoad, RejectsIrEntriesWithPathSeparators)
{
    const fs::path dir   = uniqueDir("escape");
    const fs::path scene = dir / "level.json";

    nlohmann::json idx;
    idx["probes"] = nlohmann::json::array({
        { { "id", 1 }, { "position", { 0.0, 0.0, 0.0 } }, { "ir", "../../etc/passwd" } },  // rejected
        { { "id", 2 }, { "position", { 0.0, 0.0, 0.0 } }, { "ir", "sub/probe.wav" } },      // rejected
        { { "id", 3 }, { "position", { 0.0, 0.0, 0.0 } }, { "ir", "probe_3.wav" } },        // kept
    });

    const auto probes = loadAcousticsIndex(writeIndexBeside(scene, idx));
    ASSERT_EQ(probes.size(), 1u);
    EXPECT_EQ(probes[0].id, 3u);
    fs::remove_all(dir);
}

TEST(AcousticsSidecarDir, DerivesFromSceneStemAndEmptyForInMemory)
{
    // The read path (loadAcousticsIndex) and the write path (bakeAcoustics) must
    // agree on this directory — it is the single source of truth for both.
    EXPECT_EQ(fs::path(acousticsSidecarDir("/a/b/temple.json")),
              fs::path("/a/b") / "temple_acoustics");
    EXPECT_EQ(fs::path(acousticsSidecarDir("hall.scene")),
              fs::path("hall_acoustics"));
    EXPECT_TRUE(acousticsSidecarDir("").empty());  // in-memory scene → nowhere.
}

TEST(ReverbSystemBake, UninitialisedOrUnsavedSceneWritesNothing)
{
    ReverbSystem reverb;  // never initialize()'d → no engine handle.

    // An in-memory scene has no source path either; both guards must hold, so the
    // bake reports failure and touches no disk.
    Scene scene("unsaved");
    EXPECT_TRUE(scene.getSourcePath().empty());
    const AcousticBakeResult result = reverb.bakeAcoustics(scene);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.probes.empty());
}

TEST(AcousticsIndexLoad, NearestLookupSelectsClosestBakedProbe)
{
    const fs::path dir   = uniqueDir("nearest");
    const fs::path scene = dir / "hall.json";

    nlohmann::json idx;
    idx["probes"] = nlohmann::json::array({
        { { "id", 10 }, { "position", { 0.0, 0.0, 0.0 } }, { "ir", "probe_10.wav" } },
        { { "id", 20 }, { "position", { 10.0, 0.0, 0.0 } }, { "ir", "probe_20.wav" } },
    });

    const auto probes = loadAcousticsIndex(writeIndexBeside(scene, idx));
    ASSERT_EQ(probes.size(), 2u);

    std::vector<glm::vec3> positions;
    for (const LoadedAcousticProbe& p : probes)
    {
        positions.push_back(p.position);
    }

    // The runtime picks the nearest probe (same pure lookup the placement/bake
    // code uses): near the origin → probe 0; near x=10 → probe 1.
    EXPECT_EQ(nearestAcousticProbeIndex(positions, glm::vec3(1.0f, 0.0f, 0.0f)), 0);
    EXPECT_EQ(nearestAcousticProbeIndex(positions, glm::vec3(9.0f, 0.0f, 0.0f)), 1);

    fs::remove_all(dir);
}
