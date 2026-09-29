// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_update_installer.cpp
/// @brief 3D_E-0729 installers: which kinds may install (INV-7), a failed
///        install leaves the old version whole (INV-5), the zip is unpacked
///        safely, and a relaunch drops AppRun's --assets pair. Temp dirs only;
///        nothing is executed.

#include <gtest/gtest.h>

#include "update/update_installer.h"

#include "test_helpers.h"

#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Vestige::Update::Test
{
namespace
{

class UpdateInstallerTest : public ::testing::Test
{
protected:
    fs::path m_root;

    void SetUp() override
    {
        m_root = fs::temp_directory_path()
               / ("vestige_update_installer_" + ::Vestige::Testing::vestigeTestStamp());
        fs::remove_all(m_root);
        fs::create_directories(m_root);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::permissions(m_root, fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(m_root, ec);
    }

    static std::string read(const fs::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }
    static void write(const fs::path& p, const std::string& text)
    {
        std::ofstream(p, std::ios::binary) << text;
    }
    static std::vector<std::uint8_t> bytes(const std::string& s)
    {
        return {s.begin(), s.end()};
    }
    /// A zip in memory holding @a files (name -> contents).
    static std::vector<std::uint8_t> makeZip(const std::vector<std::pair<std::string, std::string>>& files)
    {
        mz_zip_archive zip{};
        EXPECT_TRUE(mz_zip_writer_init_heap(&zip, 0, 0));
        for (const auto& [name, data] : files)
        {
            EXPECT_TRUE(mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(),
                                              MZ_DEFAULT_COMPRESSION));
        }
        void* buf = nullptr;
        size_t size = 0;
        EXPECT_TRUE(mz_zip_writer_finalize_heap_archive(&zip, &buf, &size));
        std::vector<std::uint8_t> out(static_cast<std::uint8_t*>(buf),
                                      static_cast<std::uint8_t*>(buf) + size);
        mz_free(buf);
        mz_zip_writer_end(&zip);
        return out;
    }
};

// INV-7
TEST_F(UpdateInstallerTest, OnlyAppImageAndWindowsZipCanInstall)
{
    EXPECT_EQ(installerFor(InstallKind::None, {}), nullptr);
    EXPECT_EQ(installerFor(InstallKind::Tarball, {}), nullptr);
    EXPECT_NE(installerFor(InstallKind::AppImage, {}), nullptr);
    EXPECT_NE(installerFor(InstallKind::WindowsZip, {}), nullptr);
}

TEST_F(UpdateInstallerTest, AppImageIsReplacedAndExecutable)
{
    const fs::path image = m_root / "Vestige.AppImage";
    write(image, "old");
    InstallContext ctx;
    ctx.appimagePath = image;
    auto installer = installerFor(InstallKind::AppImage, ctx);
    ASSERT_EQ(installer->apply(bytes("new build"), "0.1.76"), "");
    EXPECT_EQ(read(image), "new build");
    EXPECT_NE(fs::status(image).permissions() & fs::perms::owner_exec, fs::perms::none);
    EXPECT_EQ(std::distance(fs::directory_iterator(m_root), fs::directory_iterator{}), 1)
        << "a temporary file was left behind";
}

// INV-5
TEST_F(UpdateInstallerTest, FailedAppImageInstallLeavesTheOldFileWhole)
{
    const fs::path image = m_root / "Vestige.AppImage";
    write(image, "old");
    fs::permissions(m_root, fs::perms::owner_write, fs::perm_options::remove);
    if (std::ofstream(m_root / "probe"))
    {
        GTEST_SKIP() << "running with permission to write a read-only folder (root?)";
    }
    InstallContext ctx;
    ctx.appimagePath = image;
    EXPECT_NE(installerFor(InstallKind::AppImage, ctx)->apply(bytes("new"), "0.1.76"), "");
    fs::permissions(m_root, fs::perms::owner_write, fs::perm_options::add);
    EXPECT_EQ(read(image), "old");
    EXPECT_EQ(std::distance(fs::directory_iterator(m_root), fs::directory_iterator{}), 1);
}

TEST_F(UpdateInstallerTest, WindowsZipIsStagedBesideTheInstall)
{
    const fs::path install = m_root / "vestige-0.1.75-windows-x86_64";
    fs::create_directories(install);
    write(install / "vestige.exe", "old");
    InstallContext ctx;
    ctx.installDir = install;
    auto installer = installerFor(InstallKind::WindowsZip, ctx);
    const auto zip = makeZip({{"vestige-0.1.76-windows-x86_64/vestige.exe", "new"},
                              {"vestige-0.1.76-windows-x86_64/assets/a.txt", "asset"}});
    ASSERT_EQ(installer->apply(zip, "0.1.76"), "");
    const fs::path staged = m_root / "vestige-0.1.75-windows-x86_64.update-0.1.76"
                          / "vestige-0.1.76-windows-x86_64";
    EXPECT_EQ(read(staged / "vestige.exe"), "new");
    EXPECT_EQ(read(staged / "assets" / "a.txt"), "asset");
    EXPECT_EQ(read(install / "vestige.exe"), "old");  // untouched until the helper runs
}

TEST_F(UpdateInstallerTest, WindowsZipWithoutTheExeIsRejectedAndCleanedUp)
{
    const fs::path install = m_root / "vestige";
    fs::create_directories(install);
    InstallContext ctx;
    ctx.installDir = install;
    auto installer = installerFor(InstallKind::WindowsZip, ctx);
    EXPECT_NE(installer->apply(makeZip({{"vestige.exe", "flat, no folder"}}), "0.1.76"), "");
    EXPECT_FALSE(fs::exists(m_root / "vestige.update-0.1.76"));
}

TEST_F(UpdateInstallerTest, ZipEntriesCannotEscapeTheirFolder)
{
    const fs::path dest = m_root / "dest";
    fs::create_directories(dest);
    const auto evil = makeZip({{"ok.txt", "fine"}, {"../escaped.txt", "evil"}});
    EXPECT_NE(extractZip(evil, dest), "");
    EXPECT_FALSE(fs::exists(m_root / "escaped.txt"));
}

TEST_F(UpdateInstallerTest, CleanupRemovesOnlyTheOldWindowsFolder)
{
    const fs::path install = m_root / "vestige";
    fs::create_directories(install.string() + ".old");
    cleanupAfterUpdate(InstallKind::AppImage, install);
    EXPECT_TRUE(fs::exists(install.string() + ".old"));
    cleanupAfterUpdate(InstallKind::WindowsZip, install);
    EXPECT_FALSE(fs::exists(install.string() + ".old"));
}

TEST(UpdateRelaunch, DropsAppRunsAssetsPairAndKeepsTheUsersArguments)
{
    const char* appdir = "/tmp/.mount_Vabc";
    EXPECT_EQ(userArguments({"vestige", "--assets", "/tmp/.mount_Vabc/usr/share/vestige/assets",
                             "--scene", "a.scene"},
                            appdir),
              (std::vector<std::string>{"--scene", "a.scene"}));
    // A user's own --assets pointing elsewhere is kept.
    EXPECT_EQ(userArguments({"vestige", "--assets", "/home/u/assets"}, appdir),
              (std::vector<std::string>{"--assets", "/home/u/assets"}));
    EXPECT_EQ(userArguments({"vestige", "--play"}, nullptr),
              (std::vector<std::string>{"--play"}));
}

}  // namespace
}  // namespace Vestige::Update::Test
