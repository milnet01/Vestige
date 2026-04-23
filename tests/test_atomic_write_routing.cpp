// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_atomic_write_routing.cpp
/// @brief Phase 10.9 Slice 1 F7 — atomic-write unification contract.
///
/// Contract (authored from engine/utils/atomic_write.h and ROADMAP Phase
/// 10.9 Slice 1 F7):
///
///   The canonical durable-write helper is `AtomicWrite::writeFile`. It
///   implements the POSIX-mandated write-temp / fsync / rename /
///   fsync-dir dance (Calvin Loncaric, "How to Durably Write a File on
///   POSIX", and the documented `rename(2)` atomicity guarantee). Every
///   engine persistence path must route through that helper — the scene
///   file, the prefab library, the window-state sidecar, the auto-save
///   .path breadcrumb, and the terrain heightmap / splatmap — so the
///   durability guarantees ship consistently and duplicated
///   implementations can't silently drop a step.
///
/// Before F7, `PrefabSystem::savePrefab` wrote directly via
/// `std::ofstream`, never touching any `.tmp` sidecar. A crash-leftover
/// `<target>.tmp` from a prior aborted save therefore survived the
/// subsequent successful save — durability-blind and in direct
/// violation of CLAUDE.md Rule 3 ("Reuse before rewriting").
///
/// This test plants a stale `.tmp` sidecar alongside the target, calls
/// `savePrefab`, and asserts:
///   (a) the target file was written with the new contents, and
///   (b) the stale `.tmp` sidecar has been replaced (atomic-write's
///       write-to-tmp + rename cycle overwrites any prior .tmp).
///
/// (b) fails against a direct-`ofstream` save path that never opens the
/// `.tmp` at all. It passes once `savePrefab` is routed through
/// `AtomicWrite::writeFile`.

#include <gtest/gtest.h>

#include "editor/prefab_system.h"
#include "resource/resource_manager.h"
#include "scene/entity.h"
#include "scene/scene.h"

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

class AtomicWriteRoutingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Unique per-process so `ctest -j` runs don't step on one
        // another's assets directory.
        m_assetsDir = fs::temp_directory_path()
                    / ("vestige_f7_prefab_" + std::to_string(VESTIGE_GETPID()));
        std::error_code ec;
        fs::remove_all(m_assetsDir, ec);
        fs::create_directories(m_assetsDir / "prefabs");
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_assetsDir, ec);
    }

    fs::path m_assetsDir;
};

} // namespace

// ---------------------------------------------------------------------------
// PrefabSystem::savePrefab must route through the canonical atomic-write
// helper. Stale `.tmp` sidecar from a previously crashed save must not
// survive the next successful save.
// ---------------------------------------------------------------------------

TEST_F(AtomicWriteRoutingTest,
       POSIXRename_PrefabSaveClearsStaleTmpSidecar_Rule3)
{
    // Plant a stale `.tmp` sidecar — simulates a crash mid-save from a
    // prior process that wrote `TestPrefab.json.tmp` but never completed
    // the rename step.
    const fs::path targetPath = m_assetsDir / "prefabs" / "TestPrefab.json";
    fs::path stalePath = targetPath;
    stalePath += ".tmp";
    {
        std::ofstream stale(stalePath);
        stale << "stale garbage from crashed save";
    }
    ASSERT_TRUE(fs::exists(stalePath))
        << "precondition: stale .tmp sidecar must exist before save";

    // Save a fresh prefab under the same name.
    Scene scene("F7");
    Entity* e = scene.createEntity("Root");
    ASSERT_NE(e, nullptr);

    ResourceManager resources;
    PrefabSystem prefabs;
    const bool ok = prefabs.savePrefab(*e, "TestPrefab", resources,
                                       m_assetsDir.string());
    ASSERT_TRUE(ok) << "savePrefab returned false";

    // (a) Target written.
    EXPECT_TRUE(fs::exists(targetPath))
        << "savePrefab did not produce " << targetPath;

    // (b) Stale .tmp sidecar cleared. A direct-`ofstream` write path
    //     (pre-F7) never opens the .tmp at all, so the stale file
    //     survives and this assertion fails — exactly the durability-
    //     blind savePrefab the F7 refactor is pinning.
    EXPECT_FALSE(fs::exists(stalePath))
        << "stale .tmp sidecar survived savePrefab — atomic-write "
           "helper was not invoked (CLAUDE.md Rule 3: reuse before "
           "rewriting; engine/utils/atomic_write.h contract).";
}

// ---------------------------------------------------------------------------
// Fresh-directory save (no stale sidecar) must still leave no .tmp
// artifact. This pins the "post-rename, .tmp is gone" tail of the
// atomic-write contract.
// ---------------------------------------------------------------------------

TEST_F(AtomicWriteRoutingTest,
       POSIXRename_PrefabSaveLeavesNoTmpArtifact)
{
    Scene scene("F7");
    Entity* e = scene.createEntity("Root");
    ASSERT_NE(e, nullptr);

    ResourceManager resources;
    PrefabSystem prefabs;
    ASSERT_TRUE(prefabs.savePrefab(*e, "FreshSave", resources,
                                   m_assetsDir.string()));

    const fs::path targetPath = m_assetsDir / "prefabs" / "FreshSave.json";
    fs::path tmpPath = targetPath;
    tmpPath += ".tmp";

    EXPECT_TRUE(fs::exists(targetPath));
    EXPECT_FALSE(fs::exists(tmpPath))
        << "savePrefab left a .tmp sidecar — rename step did not "
           "complete (engine/utils/atomic_write.h contract).";
}
