// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file string_table.h
/// @brief Key→value translation table for one language (Phase 10 Localization
///        L4). See docs/phases/phase_10_localization_design.md § 5.5.
///
/// Out-of-scope items deferred to Phase 11+ (design § 6): full Unicode BiDi
/// (UAX#9), Arabic/Persian/Devanagari/CJK shaping (needs HarfBuzz), CJK
/// rendering, plural forms (ngettext), date/number formatting, editor (ImGui)
/// i18n, and RTL layout for menu chrome.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#if defined(VESTIGE_LOCALIZATION_WARN_MISSING)
#include <unordered_set>
#endif

namespace Vestige
{

/// @brief Key→value lookup table loaded from a JSON file per language.
///
/// Keys are dot-delimited paths ("plaque.tabernacle.title"); values are UTF-8
/// strings. The JSON schema is flat (no nesting) — dot-delimited keys only
/// look hierarchical; the loader is a single `json.items()` walk.
class StringTable
{
public:
    /// @brief Load a localization JSON file from `path`.
    /// @return false on missing file, parse error, or oversized payload
    ///         (`JsonSizeCap` default applies). On a successful load the map
    ///         is replaced; on failure the current map is left intact.
    bool loadFromFile(const std::string& path);

    /// @brief Lookup. A missing key ALWAYS returns the key itself (every
    ///        build — pins § 8 test 14). When built with
    ///        `VESTIGE_LOCALIZATION_WARN_MISSING` (default in debug), the miss
    ///        is logged once per session per key. The returned view's lifetime
    ///        is the table's value storage on a hit, or the caller's `key`
    ///        argument on a miss — materialise to std::string at the call site
    ///        (design § 5.5).
    std::string_view get(std::string_view key) const;

    /// @brief True iff the table has a value for this key.
    bool contains(std::string_view key) const;

    /// @brief Number of loaded keys. Used by the editor "missing keys" overlay.
    size_t size() const;

private:
    std::unordered_map<std::string, std::string> m_map;
#if defined(VESTIGE_LOCALIZATION_WARN_MISSING)
    mutable std::unordered_set<std::string> m_loggedMisses;
#endif
};

} // namespace Vestige
