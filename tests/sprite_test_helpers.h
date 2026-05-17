// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_test_helpers.h
/// @brief Shared sprite-test scratch-dir helper.
///
/// /test-audit 2026-05-17 Ts19-D5: three sprite tests
/// (test_sprite_atlas.cpp, test_sprite_panel.cpp, test_sprite_renderer.cpp)
/// each had their own copy of the same "make per-test scratch dir, write
/// JSON atlas, return path" code. They differ in JSON content but the
/// directory + file-counter scaffolding is identical. Extracted here so a
/// single change to the temp-dir scheme touches one file, not three.
#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace Vestige::Testing
{

/// @brief Writes @p content to a per-test scratch file and returns the
///        path. Each ctest process gets its own directory rooted at
///        `<temp>/vestige_<area>_test_<test-name>`, so parallel runs
///        cannot collide on a fixed-name atlas JSON.
inline std::string writeAtlasJsonScratch(const std::string& area,
                                         const std::string& fileName,
                                         const std::string& content)
{
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string key = info ? info->name() : "unknown";
    auto dir = std::filesystem::temp_directory_path()
             / ("vestige_" + area + "_test_" + key);
    std::filesystem::create_directories(dir);
    const auto path = dir / fileName;
    std::ofstream out(path);
    out << content;
    return path.string();
}

}  // namespace Vestige::Testing
