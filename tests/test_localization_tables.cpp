// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_localization_tables.cpp
/// @brief 3D_E-0024 — every shipped language is accepted and has a table
///        (INV-4), and the UI font loads the glyphs its tables use (INV-5).
#include "core/settings.h"
#include "localization/supported_languages.h"
#include "renderer/text_renderer.h"
#include "ui/graphics_settings_page.h"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Vestige;

TEST(LocalizationTables, EveryShippedLanguageIsAcceptedAndHasATable)
{
    for (const SupportedLanguage& lang : kSupportedLanguages)
    {
        Settings s;
        s.localization.language = lang.code;
        validate(s);
        EXPECT_EQ(s.localization.language, lang.code)
            << lang.code << " is shipped but settings validation rejects it";

        const std::filesystem::path table =
            std::filesystem::path(VESTIGE_LOCALIZATION_DIR) / (std::string(lang.code) + ".json");
        EXPECT_TRUE(std::filesystem::is_regular_file(table))
            << lang.code << " is shipped without " << table;
    }
}

// 3D_E-0035 INV-10: LocalizationAuditStrict checks English only, so this is
// what keeps the Settings page translated in every shipped language.
TEST(LocalizationTables, SettingsPageKeysInEveryTable)
{
    const auto keys = settingsPageKeys();
    ASSERT_FALSE(keys.empty());
    for (const SupportedLanguage& lang : kSupportedLanguages)
    {
        std::ifstream in(std::filesystem::path(VESTIGE_LOCALIZATION_DIR)
                         / (std::string(lang.code) + ".json"));
        ASSERT_TRUE(in.good()) << lang.code;
        const nlohmann::json table = nlohmann::json::parse(in);
        for (std::string_view key : keys)
        {
            EXPECT_TRUE(table.contains(std::string(key)))
                << lang.code << ".json lacks " << key;
        }
    }
}

TEST(LocalizationTables, UnshippedLanguageFallsBackToEnglish)
{
    Settings s;
    s.localization.language = "xx";
    validate(s);
    EXPECT_EQ(s.localization.language, "en");
}

TEST(LocalizationTables, UiFontLoadsAccentedLatinAndPunctuation)
{
    const auto& ranges = primaryUiGlyphRanges();
    auto covered = [&ranges](std::uint32_t cp)
    {
        for (const CodepointRange& r : ranges)
        {
            if (cp >= r.firstInclusive && cp <= r.lastInclusive)
            {
                return true;
            }
        }
        return false;
    };
    for (std::uint32_t cp : {0x00E9u /* é */, 0x00F1u /* ñ */, 0x0153u /* œ */, 0x2014u /* — */})
    {
        EXPECT_TRUE(covered(cp)) << "codepoint 0x" << std::hex << cp << " not loaded";
    }
}
