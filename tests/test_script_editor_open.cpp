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

} // namespace Vestige::Test
