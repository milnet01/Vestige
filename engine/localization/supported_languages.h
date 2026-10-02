// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file supported_languages.h
/// @brief 3D_E-0024 — the one list of interface languages the engine ships.
///
/// Settings validation and the editor's language picker both read it. Each
/// code's string table is `assets/localization/<code>.json`.
#pragma once

#include <array>
#include <string_view>

namespace Vestige
{

/// @brief One shipped interface language.
struct SupportedLanguage
{
    const char* code;         ///< BCP 47 tag and table file stem ("pt-BR").
    const char* englishName;  ///< Name in English ("Portuguese (Brazil)").
    const char* nativeName;   ///< Name in the language itself ("Português (Brasil)").
};

/// @brief Every shipped language, English first.
inline constexpr std::array<SupportedLanguage, 9> kSupportedLanguages = {{
    {"en",    "English",             "English"},
    {"el",    "Greek",               "Ελληνικά"},
    {"he",    "Hebrew",              "עברית"},
    {"la",    "Latin",               "Latina"},
    {"fr",    "French",              "Français"},
    {"de",    "German",              "Deutsch"},
    {"es",    "Spanish",             "Español"},
    {"it",    "Italian",             "Italiano"},
    {"pt-BR", "Portuguese (Brazil)", "Português (Brasil)"},
}};

/// @brief True when `code` is one of `kSupportedLanguages`.
constexpr bool isSupportedLanguage(std::string_view code)
{
    for (const SupportedLanguage& lang : kSupportedLanguages)
    {
        if (code == lang.code)
        {
            return true;
        }
    }
    return false;
}

} // namespace Vestige
