// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_texture_reload.cpp
/// @brief 3D_E-0874 lane 14 — an image changed on disk shows its new
///        contents. The asset browser kept the old thumbnail for good, and
///        ResourceManager kept serving the old texture to every material.
///
/// The cached Texture is reloaded in place rather than dropped, because
/// materials hold it and findTexturePath finds a texture by that instance.
#include <gtest/gtest.h>

#include "editor/panels/asset_browser_panel.h"
#include "gl_test_fixture.h"
#include "renderer/texture.h"
#include "resource/resource_manager.h"
#include "test_helpers.h"

#include <imgui.h>
#include <stb_image_write.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace Vestige::Test
{

namespace
{

/// Writes a size x size opaque grey PNG.
bool writeSquarePng(const fs::path& file, int size)
{
    const std::vector<std::uint8_t> pixels(static_cast<size_t>(size * size * 4), 128);
    return stbi_write_png(file.string().c_str(), size, size, 4, pixels.data(), size * 4) != 0;
}

/// Moves the file's write time forward, so a poll that compares write times
/// sees a change even when both writes land in the same clock tick.
void bumpWriteTime(const fs::path& file)
{
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(10));
}

fs::path makeRoot(const char* name)
{
    const fs::path root = fs::temp_directory_path()
        / (std::string(name) + Testing::vestigeTestStamp());
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

} // namespace

class TextureReloadTest : public GLTestFixture {};

TEST_F(TextureReloadTest, ReloadRefreshesBothCachedFormsInPlace)
{
    const fs::path root = makeRoot("vestige_texture_reload_");
    const fs::path png = root / "a.png";
    ASSERT_TRUE(writeSquarePng(png, 2));

    ResourceManager resources;
    resources.setSandboxRoots({root});
    resources.setAssetRoot(root);
    auto srgb = resources.loadTexture(png.string(), false);
    auto linear = resources.loadTexture(png.string(), true);
    ASSERT_EQ(srgb->getWidth(), 2);
    ASSERT_EQ(linear->getWidth(), 2);
    srgb->setFilterMode(TextureFilterMode::NEAREST);

    ASSERT_TRUE(writeSquarePng(png, 4));
    EXPECT_TRUE(resources.reloadTexture(png.string()));

    // The same instances, now holding the new image.
    EXPECT_EQ(resources.loadTexture(png.string(), false), srgb);
    EXPECT_EQ(resources.loadTexture(png.string(), true), linear);
    EXPECT_EQ(srgb->getWidth(), 4);
    EXPECT_EQ(linear->getWidth(), 4);
    EXPECT_EQ(srgb->getFilterMode(), TextureFilterMode::NEAREST);
    EXPECT_EQ(resources.findTexturePath(srgb), "a.png");

    fs::remove_all(root);
}

TEST_F(TextureReloadTest, AFailedReloadKeepsTheOldImage)
{
    const fs::path root = makeRoot("vestige_texture_reload_bad_");
    const fs::path png = root / "a.png";
    ASSERT_TRUE(writeSquarePng(png, 2));

    ResourceManager resources;
    resources.setSandboxRoots({root});
    resources.setAssetRoot(root);
    auto tex = resources.loadTexture(png.string(), false);
    ASSERT_EQ(tex->getWidth(), 2);

    // A half-written file: an editor saving it while the browser polls.
    {
        std::ofstream out(png, std::ios::binary | std::ios::trunc);
        out << "not an image";
    }
    EXPECT_FALSE(resources.reloadTexture(png.string()));
    EXPECT_TRUE(tex->isLoaded());
    EXPECT_EQ(tex->getWidth(), 2);

    fs::remove_all(root);
}

TEST_F(TextureReloadTest, AnUncachedFileIsLeftAlone)
{
    const fs::path root = makeRoot("vestige_texture_reload_uncached_");
    const fs::path png = root / "a.png";
    ASSERT_TRUE(writeSquarePng(png, 2));

    ResourceManager resources;
    resources.setSandboxRoots({root});
    resources.setAssetRoot(root);
    EXPECT_FALSE(resources.reloadTexture(png.string()));
    EXPECT_EQ(resources.getTextureCount(), 0u);

    fs::remove_all(root);
}

/// The texture ids the last ImGui frame drew with ImGui::Image.
std::set<ImTextureID> drawnImageIds()
{
    std::set<ImTextureID> ids;
    const ImDrawData* data = ImGui::GetDrawData();
    for (const ImDrawList* list : data->CmdLists)
    {
        for (const ImDrawCmd& cmd : list->CmdBuffer)
        {
            if (cmd.TexRef._TexData == nullptr)
            {
                ids.insert(cmd.TexRef._TexID);
            }
        }
    }
    return ids;
}

TEST_F(TextureReloadTest, AssetBrowserShowsAChangedImage)
{
    const fs::path root = makeRoot("vestige_texture_reload_browser_");
    const fs::path png = root / "a.png";
    ASSERT_TRUE(writeSquarePng(png, 2));

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640.0f, 480.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();

    ResourceManager resources;
    resources.setSandboxRoots({root});
    resources.setAssetRoot(root);
    AssetBrowserPanel panel;
    panel.setPollInterval(std::chrono::milliseconds(0));
    panel.initialize(root.string(), resources);

    auto drawFrame = [&panel]()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin("assets");
        panel.draw();
        ImGui::End();
        ImGui::Render();
    };

    for (int i = 0; i < 3; ++i)
    {
        drawFrame();
    }
    const auto before = resources.loadTexture(png.string(), false);
    ASSERT_EQ(before->getWidth(), 2);
    ASSERT_EQ(drawnImageIds().count(static_cast<ImTextureID>(before->getId())), 1u)
        << "the thumbnail must be drawn before the change";

    ASSERT_TRUE(writeSquarePng(png, 4));
    bumpWriteTime(png);
    for (int i = 0; i < 3; ++i)
    {
        drawFrame();
    }

    const auto after = resources.loadTexture(png.string(), false);
    EXPECT_EQ(after->getWidth(), 4);
    EXPECT_EQ(drawnImageIds().count(static_cast<ImTextureID>(after->getId())), 1u)
        << "the browser must draw the reloaded texture";

    ImGui::DestroyContext();
    fs::remove_all(root);
}

} // namespace Vestige::Test
