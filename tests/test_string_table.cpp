// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_string_table.cpp
/// @brief Tests for the localization StringTable (Phase 10 Localization L4).
///        See docs/phases/phase_10_localization_design.md § 5.5
///        and § 8 tests 13-15.
#include <gtest/gtest.h>

#include "localization/string_table.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace Vestige;
namespace fs = std::filesystem;

namespace
{
// Writes `content` to a unique temp .json file and returns its path. The
// file is left on disk for the duration of the test process (system temp
// dir); tests are self-contained and do not depend on repo assets.
fs::path writeTempJson(const std::string& tag, const std::string& content)
{
    static int counter = 0;
    fs::path p = fs::temp_directory_path()
                 / ("vestige_strtab_" + tag + "_" + std::to_string(counter++) + ".json");
    std::ofstream f(p);
    f << content;
    f.close();
    return p;
}
} // namespace

// Test 13 — after loading a table, a present key resolves to its value.
TEST(StringTable, LookupHit)
{
    const fs::path p = writeTempJson("hit", R"({
        "ui.menu.start_game": "Start Game",
        "ui.menu.settings": "Settings"
    })");

    StringTable t;
    ASSERT_TRUE(t.loadFromFile(p.string()));
    EXPECT_EQ(t.get("ui.menu.start_game"), "Start Game");
    EXPECT_TRUE(t.contains("ui.menu.start_game"));
    EXPECT_EQ(t.size(), 2u);
}

// Test 14 — a missing key returns the key itself (silent passthrough).
TEST(StringTable, LookupMissReturnsKey)
{
    const fs::path p = writeTempJson("miss", R"({ "present.key": "value" })");

    StringTable t;
    ASSERT_TRUE(t.loadFromFile(p.string()));
    EXPECT_EQ(t.get("missing.key"), "missing.key");
    EXPECT_FALSE(t.contains("missing.key"));
}

// Test 15 — loading a missing file returns false and leaves a prior load
// intact (pins boot-without-translations behaviour).
TEST(StringTable, LoadMissingFileReturnsFalse)
{
    const fs::path good = writeTempJson("prior", R"({ "kept.key": "kept" })");

    StringTable t;
    ASSERT_TRUE(t.loadFromFile(good.string()));
    EXPECT_EQ(t.get("kept.key"), "kept");

    EXPECT_FALSE(t.loadFromFile("/does/not/exist/vestige_nope.json"));

    // Prior load survives the failed reload.
    EXPECT_EQ(t.get("kept.key"), "kept");
    EXPECT_EQ(t.size(), 1u);
}
