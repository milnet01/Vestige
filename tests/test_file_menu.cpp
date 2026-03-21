/// @file test_file_menu.cpp
/// @brief Unit tests for FileMenu, RecentFiles, and auto-save.
#include "editor/file_menu.h"
#include "editor/recent_files.h"
#include "scene/scene.h"
#include "editor/selection.h"
#include "editor/scene_serializer.h"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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
        m_testDir = fs::temp_directory_path() / "vestige_test_file_menu";
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

TEST_F(FileMenuTest, MarkDirtySetsFlag)
{
    FileMenu menu;
    menu.markDirty();
    EXPECT_TRUE(menu.isDirty());
}

TEST_F(FileMenuTest, MarkCleanClearsFlag)
{
    FileMenu menu;
    menu.markDirty();
    EXPECT_TRUE(menu.isDirty());

    menu.markClean();
    EXPECT_FALSE(menu.isDirty());
}

TEST_F(FileMenuTest, MultipleMarkDirtyIdempotent)
{
    FileMenu menu;
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
    menu.markDirty();

    // No window set — requestQuit shows modal but doesn't set shouldQuit
    menu.requestQuit();
    EXPECT_FALSE(menu.shouldQuit());
}

TEST_F(FileMenuTest, RequestQuitWhenDirtyIgnoresDuplicate)
{
    FileMenu menu;
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
        m_testDir = fs::temp_directory_path() / "vestige_test_recent";
        fs::create_directories(m_testDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
    }

    /// @brief Creates a dummy file at the given path.
    void createDummyFile(const fs::path& path)
    {
        std::ofstream out(path);
        out << "dummy";
    }

    fs::path m_testDir;
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
