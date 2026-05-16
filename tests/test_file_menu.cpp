// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_file_menu.cpp
/// @brief Unit tests for FileMenu, RecentFiles, and auto-save.
#include "editor/file_menu.h"
#include "editor/recent_files.h"
#include "editor/command_history.h"
#include "scene/scene.h"
#include "editor/selection.h"
#include "editor/scene_serializer.h"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "test_helpers.h"

using namespace Vestige;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture — no GPU context needed (FileMenu logic is pure state)
// ---------------------------------------------------------------------------

class FileMenuTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Unique per-process: see rationale in test_scene_serializer.cpp.
        m_testDir = fs::temp_directory_path()
                  / ("vestige_test_file_menu_" + Testing::vestigeTestStamp());
        fs::create_directories(m_testDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
    }

    /// @brief Helper: writes a minimal valid scene file.
    void writeMinimalSceneFile(const fs::path& path)
    {
        nlohmann::json j;
        j["vestige_scene"]["format_version"] = SceneSerializer::CURRENT_FORMAT_VERSION;
        j["vestige_scene"]["name"] = "TestScene";
        j["vestige_scene"]["engine_version"] = SceneSerializer::ENGINE_VERSION;
        j["entities"] = nlohmann::json::array();

        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << j.dump(4);
        out.close();
    }

    fs::path m_testDir;
};

// ---------------------------------------------------------------------------
// Dirty state
// ---------------------------------------------------------------------------

TEST_F(FileMenuTest, StartsClean)
{
    FileMenu menu;
    EXPECT_FALSE(menu.isDirty());
}

// Phase 10.9 Slice 12 Ed7: FileMenu's dirty signal is now a thin
// wrapper around CommandHistory's isDirty / markUnsavedChange / markSaved.
// Tests below wire a CommandHistory the way production does
// (Editor::initialize calls FileMenu::setCommandHistory at startup),
// so the dirty contract is exercised end-to-end. Without a wired
// CommandHistory, FileMenu::isDirty stays false — the production
// path never hits that branch, but the StartsClean test explicitly
// pins that fallback behaviour for any caller that constructs a
// FileMenu without immediately wiring history.

// Slice 18 Ts4: dropped `MarkDirtySetsFlag` — `MarkCleanClearsFlag`
// below subsumes it (the body markDirty's then asserts isDirty before
// the clean step).

TEST_F(FileMenuTest, MarkCleanClearsFlag)
{
    FileMenu menu;
    CommandHistory history;
    menu.setCommandHistory(&history);
    menu.markDirty();
    EXPECT_TRUE(menu.isDirty());

    menu.markClean();
    EXPECT_FALSE(menu.isDirty());
}

TEST_F(FileMenuTest, MultipleMarkDirtyIdempotent)
{
    FileMenu menu;
    CommandHistory history;
    menu.setCommandHistory(&history);
    menu.markDirty();
    menu.markDirty();
    menu.markDirty();
    EXPECT_TRUE(menu.isDirty());

    menu.markClean();
    EXPECT_FALSE(menu.isDirty());
}

// ---------------------------------------------------------------------------
// Scene path
// ---------------------------------------------------------------------------

TEST_F(FileMenuTest, StartsWithEmptyPath)
{
    FileMenu menu;
    EXPECT_TRUE(menu.getCurrentScenePath().empty());
}

// ---------------------------------------------------------------------------
// Quit state
// ---------------------------------------------------------------------------

TEST_F(FileMenuTest, ShouldQuitDefaultFalse)
{
    FileMenu menu;
    EXPECT_FALSE(menu.shouldQuit());
}

TEST_F(FileMenuTest, RequestQuitWhenClean)
{
    FileMenu menu;
    // No window set, so requestQuit won't try to call glfwSetWindowShouldClose
    menu.requestQuit();
    EXPECT_TRUE(menu.shouldQuit());
}

TEST_F(FileMenuTest, RequestQuitWhenDirtyDoesNotQuit)
{
    FileMenu menu;
    CommandHistory history;
    menu.setCommandHistory(&history);
    menu.markDirty();

    // No window set — requestQuit shows modal but doesn't set shouldQuit
    menu.requestQuit();
    EXPECT_FALSE(menu.shouldQuit());
}

TEST_F(FileMenuTest, RequestQuitWhenDirtyIgnoresDuplicate)
{
    FileMenu menu;
    CommandHistory history;
    menu.setCommandHistory(&history);
    menu.markDirty();

    menu.requestQuit();
    EXPECT_FALSE(menu.shouldQuit());

    // Second call should be ignored (already pending)
    menu.requestQuit();
    EXPECT_FALSE(menu.shouldQuit());
}

// ---------------------------------------------------------------------------
// Window title construction
// ---------------------------------------------------------------------------

TEST_F(FileMenuTest, UpdateWindowTitleNoWindowDoesNotCrash)
{
    FileMenu menu;
    // No window set — should just return without crashing
    EXPECT_NO_THROW(menu.updateWindowTitle("Test"));
}

// ---------------------------------------------------------------------------
// RecentFiles tests
// ---------------------------------------------------------------------------

class RecentFilesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Unique per-process: see rationale in test_scene_serializer.cpp.
        m_testDir = fs::temp_directory_path()
                  / ("vestige_test_recent_" + Testing::vestigeTestStamp());
        fs::create_directories(m_testDir);

        // Phase 10.9 Slice 18 follow-up: `RecentFiles::addPath` calls
        // `save()` automatically (recent_files.cpp:135), so any test
        // that addPaths persists to disk. Without sandboxing
        // XDG_CONFIG_HOME, that disk write lands in the user's real
        // `~/.config/vestige/recent_files.json` and pollutes the
        // editor's recent-scenes list with test temp paths. Mirror
        // the pattern from `test_atomic_write_routing.cpp`'s
        // RecentFilesAtomicWriteTest.
        m_xdgRoot = fs::temp_directory_path()
                  / ("vestige_recent_xdg_" + Testing::vestigeTestStamp());
        std::error_code ec;
        fs::remove_all(m_xdgRoot, ec);
        fs::create_directories(m_xdgRoot);

        const char* prev = std::getenv("XDG_CONFIG_HOME");
        m_prevXdg = prev ? prev : "";
        setenv("XDG_CONFIG_HOME", m_xdgRoot.c_str(), 1);
    }

    void TearDown() override
    {
        if (m_prevXdg.empty())
        {
            unsetenv("XDG_CONFIG_HOME");
        }
        else
        {
            setenv("XDG_CONFIG_HOME", m_prevXdg.c_str(), 1);
        }
        std::error_code ec;
        fs::remove_all(m_xdgRoot, ec);
        fs::remove_all(m_testDir, ec);
    }

    /// @brief Creates a dummy file at the given path.
    void createDummyFile(const fs::path& path)
    {
        std::ofstream out(path);
        out << "dummy";
    }

    fs::path m_testDir;
    fs::path m_xdgRoot;
    std::string m_prevXdg;
};

TEST_F(RecentFilesTest, StartsEmpty)
{
    RecentFiles rf;
    EXPECT_TRUE(rf.getPaths().empty());
}

TEST_F(RecentFilesTest, AddPathInsertsAtFront)
{
    RecentFiles rf;

    fs::path a = m_testDir / "a.scene";
    fs::path b = m_testDir / "b.scene";
    createDummyFile(a);
    createDummyFile(b);

    rf.addPath(a);
    rf.addPath(b);

    ASSERT_EQ(rf.getPaths().size(), 2u);
    EXPECT_EQ(rf.getPaths()[0], fs::absolute(b));
    EXPECT_EQ(rf.getPaths()[1], fs::absolute(a));
}

TEST_F(RecentFilesTest, AddDuplicateMovesToFront)
{
    RecentFiles rf;

    fs::path a = m_testDir / "a.scene";
    fs::path b = m_testDir / "b.scene";
    createDummyFile(a);
    createDummyFile(b);

    rf.addPath(a);
    rf.addPath(b);
    rf.addPath(a);  // Move 'a' back to front

    ASSERT_EQ(rf.getPaths().size(), 2u);
    EXPECT_EQ(rf.getPaths()[0], fs::absolute(a));
    EXPECT_EQ(rf.getPaths()[1], fs::absolute(b));
}

TEST_F(RecentFilesTest, RemovePathWorks)
{
    RecentFiles rf;

    fs::path a = m_testDir / "a.scene";
    createDummyFile(a);

    rf.addPath(a);
    EXPECT_EQ(rf.getPaths().size(), 1u);

    rf.removePath(a);
    EXPECT_TRUE(rf.getPaths().empty());
}

TEST_F(RecentFilesTest, ClearRemovesAll)
{
    RecentFiles rf;

    fs::path a = m_testDir / "a.scene";
    fs::path b = m_testDir / "b.scene";
    createDummyFile(a);
    createDummyFile(b);

    rf.addPath(a);
    rf.addPath(b);
    EXPECT_EQ(rf.getPaths().size(), 2u);

    rf.clear();
    EXPECT_TRUE(rf.getPaths().empty());
}

TEST_F(RecentFilesTest, EnforcesMaxEntries)
{
    RecentFiles rf;

    // Create more files than MAX_ENTRIES
    for (size_t i = 0; i < RecentFiles::MAX_ENTRIES + 5; ++i)
    {
        fs::path p = m_testDir / ("scene_" + std::to_string(i) + ".scene");
        createDummyFile(p);
        rf.addPath(p);
    }

    EXPECT_EQ(rf.getPaths().size(), RecentFiles::MAX_ENTRIES);
}

TEST_F(RecentFilesTest, GetConfigDirReturnsValidPath)
{
    fs::path configDir = RecentFiles::getConfigDir();
    EXPECT_FALSE(configDir.empty());
    // Should end with "vestige"
    EXPECT_EQ(configDir.filename(), "vestige");
}
