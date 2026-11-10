// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_asset_browser_unreadable.cpp
/// @brief 3D_E-0874 lane 14 — the asset browser lists folders without
///        throwing. A folder that is deleted or unreadable while the browser
///        shows it threw std::filesystem_error out of initialize() and draw().
///
/// An unreadable folder (mode 000) throws every time, where a folder deleted
/// mid-poll throws only when the timing lines up. Root ignores the mode, so
/// the test skips there. Linux only: Windows does not honour the mode.
#include <gtest/gtest.h>

#include "editor/panels/asset_browser_panel.h"
#include "resource/resource_manager.h"
#include "test_helpers.h"

#include <imgui.h>

#include <filesystem>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Vestige::Test
{

#ifndef _WIN32

TEST(AssetBrowserUnreadable, AnUnreadableFolderDoesNotThrow)
{
    if (geteuid() == 0)
    {
        GTEST_SKIP() << "root reads a mode-000 folder";
    }
    const fs::path root = fs::temp_directory_path()
        / ("vestige_asset_browser_" + Testing::vestigeTestStamp());
    fs::create_directories(root / "locked");
    fs::permissions(root / "locked", fs::perms::none);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640.0f, 480.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();

    ResourceManager resources;
    AssetBrowserPanel panel;
    EXPECT_NO_THROW(panel.initialize((root / "locked").string(), resources));
    EXPECT_NO_THROW({
        ImGui::NewFrame();
        ImGui::Begin("assets");
        panel.draw();
        ImGui::End();
        ImGui::Render();
    });

    ImGui::DestroyContext();
    fs::permissions(root / "locked", fs::perms::owner_all);
    fs::remove_all(root);
}

#endif

} // namespace Vestige::Test
