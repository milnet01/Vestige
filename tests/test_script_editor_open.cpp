// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_editor_open.cpp
/// @brief 3D_E-0874 lane 19 — the script editor opens the file picked in
///        its Open dialog.
///
/// The dialog hands back an absolute path (imgui-filebrowser joins its
/// absolute current directory and the chosen name). ScriptGraph::loadFromFile
/// refuses every absolute path, because it guards paths read from data, so
/// Open failed on every file. The user's own pick is trusted (user decision
/// 2026-11-11): the panel opens and saves anywhere the dialog points.
#include <gtest/gtest.h>

#include "editor/panels/script_editor_panel.h"
#include "test_helpers.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace Vestige::Test
{

TEST(ScriptEditorOpen, OpensTheAbsolutePathItsDialogReturns)
{
    const fs::path dir = fs::temp_directory_path()
        / ("vestige_script_open_" + Testing::vestigeTestStamp());
    fs::create_directories(dir);
    const fs::path file = dir / "door.vscript";
    ASSERT_TRUE(file.is_absolute());

    ScriptEditorPanel writer;
    writer.graph().name = "door";
    ASSERT_TRUE(writer.saveAs(file.string()));
    ASSERT_TRUE(fs::exists(file));

    ScriptEditorPanel reader;
    EXPECT_TRUE(reader.open(file.string()));
    EXPECT_EQ(reader.graph().name, "door");
    EXPECT_FALSE(reader.isDirty());

    fs::remove_all(dir);
}

// 3D_E-0874 lane 15: a new, untouched script has no name and no nodes. Saved
// and reopened, it must open, not be mistaken for a failed load.
TEST(ScriptEditorOpen, ReopensAnEmptyNewScript)
{
    const fs::path dir = fs::temp_directory_path()
        / ("vestige_script_empty_" + Testing::vestigeTestStamp());
    fs::create_directories(dir);
    const fs::path file = dir / "blank.vscript";

    ScriptEditorPanel writer;
    writer.newGraph();
    ASSERT_TRUE(writer.saveAs(file.string()));

    ScriptEditorPanel reader;
    reader.graph().name = "previous";
    EXPECT_TRUE(reader.open(file.string()));
    EXPECT_TRUE(reader.graph().name.empty());

    fs::remove_all(dir);
}

// 3D_E-0874 lane 15: a Save As that fails keeps the script's previous path,
// so a later Save still writes where the script last saved.
TEST(ScriptEditorOpen, FailedSaveAsKeepsThePreviousPath)
{
    const fs::path dir = fs::temp_directory_path()
        / ("vestige_script_saveas_" + Testing::vestigeTestStamp());
    fs::create_directories(dir);
    const fs::path file = dir / "door.vscript";

    ScriptEditorPanel panel;
    panel.graph().name = "door";
    ASSERT_TRUE(panel.saveAs(file.string()));

    panel.graph().name = "door2";
    EXPECT_FALSE(panel.saveAs((dir / "no_such_folder" / "x.vscript").string()));
    EXPECT_TRUE(panel.save()) << "Save lost the previous path";

    ScriptEditorPanel reader;
    ASSERT_TRUE(reader.open(file.string()));
    EXPECT_EQ(reader.graph().name, "door2");

    fs::remove_all(dir);
}

} // namespace Vestige::Test
