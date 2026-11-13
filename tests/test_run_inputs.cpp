// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_run_inputs.cpp
/// @brief The record of which asset files a run read (3D_E-0972).
#include "profiler/run_inputs.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Vestige;
namespace fs = std::filesystem;

namespace
{

/// A scratch asset root holding models/a.bin, models/sub/b.bin and
/// textures/t.png, with c.txt beside the root and loose.txt directly in it.
class RunInputsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_base = fs::temp_directory_path()
                 / ("vestige_run_inputs_"
                    + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(m_base);
        m_root = m_base / "assets";
        fs::create_directories(m_root / "models" / "sub");
        write(m_root / "models" / "a.bin", "alpha");
        write(m_root / "models" / "sub" / "b.bin", "bravo!");
        fs::create_directories(m_root / "textures");
        write(m_root / "textures" / "t.png", "tex");
        write(m_base / "c.txt", "cc");
        write(m_root / "loose.txt", "loose");
        clearRunInputs();
    }

    void TearDown() override
    {
        clearRunInputs();
        fs::remove_all(m_base);
    }

    static void write(const fs::path& path, const std::string& text)
    {
        std::ofstream(path, std::ios::binary) << text;
    }

    fs::path m_base;
    fs::path m_root;
};

} // namespace

TEST_F(RunInputsTest, APathNotedTwiceCountsOnce)
{
    const std::string a = (m_root / "models" / "a.bin").string();
    const std::string b = (m_root / "models" / "sub" / "b.bin").string();
    noteRunInput(b);
    noteRunInput(a);
    noteRunInput(b);
    // A second spelling of the same file is still that file.
    noteRunInput((m_root / "models" / "sub" / ".." / "a.bin").string());
    EXPECT_EQ(notedRunInputs(), (std::vector<std::string>{a, b}));

    clearRunInputs();
    EXPECT_TRUE(notedRunInputs().empty());
}

TEST_F(RunInputsTest, GroupsByTopFolderWithContentHashes)
{
    const std::vector<RunInput> inputs = hashRunInputs(
        {(m_root / "textures" / "t.png").string(), (m_root / "models" / "sub" / "b.bin").string(),
         (m_root / "models" / "a.bin").string()},
        m_root);

    // Expected values from Python's hashlib: a group is
    // sha256("<path relative to the root>\0<file sha256>\n" for each file, sorted).
    ASSERT_EQ(inputs.size(), 2u);
    EXPECT_EQ(inputs[0].name, "models");
    EXPECT_EQ(inputs[0].sha256, "e385454b156fe4d57c957b1562b53aed91f8ba20f0a20867112b94cae1306911");
    EXPECT_EQ(inputs[0].bytes, 11u);
    EXPECT_EQ(inputs[1].name, "textures");
    EXPECT_EQ(inputs[1].sha256, "ec8bea5f4beeb2647f20d4a484c53ab169feb720506bf636f28b45899926e3a0");
    EXPECT_EQ(inputs[1].bytes, 3u);
}

TEST_F(RunInputsTest, AFileOutsideTheAssetFoldersIsNotAnInput)
{
    // Settings and saved scenes go through the same loaders.
    const std::vector<RunInput> inputs = hashRunInputs(
        {(m_base / "c.txt").string(), (m_root / "loose.txt").string(),
         (m_root / "textures" / "t.png").string()},
        m_root);
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs[0].name, "textures");
}

TEST_F(RunInputsTest, AChangedByteChangesOnlyItsGroup)
{
    const std::vector<std::string> paths = {(m_root / "textures" / "t.png").string(),
                                            (m_root / "models" / "a.bin").string()};
    const std::vector<RunInput> before = hashRunInputs(paths, m_root);
    write(m_root / "models" / "a.bin", "alphA");
    const std::vector<RunInput> after = hashRunInputs(paths, m_root);

    ASSERT_EQ(before.size(), 2u);
    ASSERT_EQ(after.size(), 2u);
    EXPECT_NE(before[0].sha256, after[0].sha256);
    EXPECT_EQ(before[0].bytes, after[0].bytes);
    EXPECT_EQ(before[1].sha256, after[1].sha256);
}

TEST_F(RunInputsTest, AFileThatIsGoneIsLeftOut)
{
    const std::vector<RunInput> inputs = hashRunInputs(
        {(m_root / "models" / "a.bin").string(), (m_root / "models" / "missing.bin").string()},
        m_root);
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs[0].bytes, 5u);
}
