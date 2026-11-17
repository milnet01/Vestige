// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_editor_shortcuts.cpp
/// @brief 3D_E-0874 lane 15 — the script editor's File menu shows Ctrl+N,
///        Ctrl+O, Ctrl+S and Ctrl+Shift+S. While the script editor has
///        focus those keys act on the script, not the scene. Before the fix
///        the panel had no key handler, so Ctrl+S saved the scene instead.
///
/// Drives the real ScriptEditorPanel and the real FileMenu shortcuts
/// headlessly, as test_hierarchy_delete_key does. The scene's side is seen
/// through FileMenu's "Unsaved Changes" prompt, which Ctrl+N opens on a
/// dirty scene.
#include <gtest/gtest.h>

#include "editor/command_history.h"
#include "editor/file_menu.h"
#include "editor/panels/script_editor_panel.h"
#include "editor/selection.h"
#include "test_helpers.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace Vestige::Test
{

class ScriptEditorShortcutsTest : public ::testing::Test
{
protected:
    FileMenu m_fileMenu;
    CommandHistory m_history;
    Selection m_selection;
    ScriptEditorPanel m_panel;
    fs::path m_dir;
    bool m_promptOpen = false;

    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        m_fileMenu.setCommandHistory(&m_history);
        m_panel.setOpen(true);
        m_dir = fs::temp_directory_path()
            / ("vestige_script_keys_" + Testing::vestigeTestStamp());
        fs::create_directories(m_dir);
    }

    void TearDown() override
    {
        ImGui::DestroyContext();
        fs::remove_all(m_dir);
    }

    // The editor's frame order: global file shortcuts first, panels later.
    void frame()
    {
        ImGui::NewFrame();
        m_fileMenu.processShortcuts(nullptr, m_selection);

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
        ImGui::Begin("Other");
        ImGui::TextUnformatted("another panel");
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(400.0f, 0.0f));
        m_panel.draw();

        m_fileMenu.drawDialogs(nullptr, m_selection);
        // Asked inside the frame: ImGui has no current window after Render().
        m_promptOpen = ImGui::IsPopupOpen("Unsaved Changes");
        ImGui::Render();
    }

    void idle(int frames)
    {
        for (int i = 0; i < frames; ++i) frame();
    }

    void clickAt(float x, float y)
    {
        ImGui::GetIO().AddMousePosEvent(x, y);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        idle(3);
    }

    void focusScriptEditor() { clickAt(600.0f, 250.0f); }
    void focusOther() { clickAt(200.0f, 250.0f); }

    void pressCtrl(ImGuiKey key)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        frame();
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        idle(2);
    }

    bool unsavedPromptOpen() const { return m_promptOpen; }
};

TEST_F(ScriptEditorShortcutsTest, CtrlSInTheScriptEditorSavesTheScript)
{
    const fs::path file = m_dir / "door.vscript";
    m_panel.graph().name = "door";
    ASSERT_TRUE(m_panel.saveAs(file.string()));
    m_panel.graph().name = "door2";

    idle(2);
    focusScriptEditor();
    pressCtrl(ImGuiKey_S);

    ScriptEditorPanel reader;
    ASSERT_TRUE(reader.open(file.string()));
    EXPECT_EQ(reader.graph().name, "door2") << "Ctrl+S did not save the script";
}

TEST_F(ScriptEditorShortcutsTest, CtrlNInTheScriptEditorDoesNotReachTheScene)
{
    m_fileMenu.markDirty();
    m_panel.graph().name = "door";

    idle(2);
    focusScriptEditor();
    pressCtrl(ImGuiKey_N);

    EXPECT_FALSE(unsavedPromptOpen()) << "Ctrl+N in the script editor acted on the scene";
    EXPECT_TRUE(m_panel.graph().name.empty()) << "Ctrl+N did not start a new script";
}

// Control: with another panel focused, Ctrl+N still reaches the scene. This
// proves the harness reaches FileMenu, so the test above is meaningful.
TEST_F(ScriptEditorShortcutsTest, CtrlNElsewhereStillReachesTheScene)
{
    m_fileMenu.markDirty();

    idle(2);
    focusOther();
    pressCtrl(ImGuiKey_N);

    EXPECT_TRUE(unsavedPromptOpen()) << "Ctrl+N no longer reaches the scene";
}

} // namespace Vestige::Test
