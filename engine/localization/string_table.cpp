// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "localization/string_table.h"

#include "utils/json_size_cap.h"

#if defined(VESTIGE_LOCALIZATION_WARN_MISSING)
#include "core/logger.h"
#endif

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Vestige
{

bool StringTable::loadFromFile(const std::string& path)
{
    // Localization files are small (≤ 32 KB even at 1000 keys); the default
    // JsonSizeCap is fine. Non-strict: a missing/oversized/malformed file logs
    // a warning and returns nullopt — the caller (LocalizationService) treats
    // that as "keep the old language" (design § 5.5 / § 5.6).
    std::optional<nlohmann::json> doc =
        JsonSizeCap::loadJsonWithSizeCap(path, "StringTable");
    if (!doc || !doc->is_object())
    {
        return false; // Leave the current map intact.
    }

    // Flat key→value schema (design § 5.5). Build into a fresh map and swap on
    // success so a partial/invalid parse never half-replaces the live table.
    std::unordered_map<std::string, std::string> fresh;
    fresh.reserve(doc->size());
    for (auto& [key, value] : doc->items())
    {
        if (value.is_string())
        {
            fresh.emplace(key, value.get<std::string>());
        }
    }

    m_map = std::move(fresh);
#if defined(VESTIGE_LOCALIZATION_WARN_MISSING)
    m_loggedMisses.clear(); // New table — past misses may now resolve.
#endif
    return true;
}

std::string_view StringTable::get(std::string_view key) const
{
    // unordered_map lacks heterogeneous lookup pre-C++20-with-custom-hash, so
    // materialise the key for the find. Lookups happen at panel-rebuild time,
    // not per-frame (design § 3), so this allocation is not on the hot path.
    auto it = m_map.find(std::string(key));
    if (it != m_map.end())
    {
        return it->second;
    }

#if defined(VESTIGE_LOCALIZATION_WARN_MISSING)
    // Warn once per session per key — no per-frame spam.
    if (m_loggedMisses.insert(std::string(key)).second)
    {
        Logger::warning("[StringTable] missing key: " + std::string(key));
    }
#endif
    return key; // Aliases the caller's argument — materialise at call site.
}

bool StringTable::contains(std::string_view key) const
{
    return m_map.find(std::string(key)) != m_map.end();
}

size_t StringTable::size() const
{
    return m_map.size();
}

std::vector<std::string> StringTable::keys() const
{
    std::vector<std::string> out;
    out.reserve(m_map.size());
    for (const auto& [key, value] : m_map)
    {
        (void)value;
        out.push_back(key);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace Vestige
