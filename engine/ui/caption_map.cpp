// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file caption_map.cpp
/// @brief Phase 10.7 slice B3 — CaptionMap implementation.
#include "ui/caption_map.h"

#include "core/logger.h"
#include "utils/json_size_cap.h"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace Vestige
{

SubtitleCategory parseSubtitleCategory(const std::string& s)
{
    if (s == "Narrator") return SubtitleCategory::Narrator;
    if (s == "SoundCue") return SubtitleCategory::SoundCue;
    return SubtitleCategory::Dialogue;
}

namespace
{

bool populateFromJson(const nlohmann::json& j,
                      std::unordered_map<std::string, Subtitle>& entries)
{
    entries.clear();
    if (!j.is_object())
    {
        Logger::warning("[CaptionMap] Root is not a JSON object; "
                        "caption map is empty.");
        return false;
    }

    for (auto it = j.begin(); it != j.end(); ++it)
    {
        const std::string& clipPath = it.key();
        const nlohmann::json& entry = it.value();
        if (!entry.is_object())
        {
            Logger::warning("[CaptionMap] Entry for '" + clipPath +
                            "' is not an object; skipped.");
            continue;
        }

        Subtitle sub;
        sub.text     = entry.value("text",    std::string{});
        sub.speaker  = entry.value("speaker", std::string{});
        sub.category = parseSubtitleCategory(
            entry.value("category", std::string{"Dialogue"}));
        const float dur = entry.value("duration",
                                       DEFAULT_CAPTION_DURATION_SECONDS);
        sub.durationSeconds = (dur > 0.0f)
            ? dur
            : DEFAULT_CAPTION_DURATION_SECONDS;

        // Skip entries with empty text — a caption with nothing to
        // display is authoring noise, not a feature.
        if (sub.text.empty())
        {
            Logger::warning("[CaptionMap] Entry for '" + clipPath +
                            "' has empty text; skipped.");
            continue;
        }

        entries.emplace(clipPath, std::move(sub));
    }
    return true;
}

} // namespace

bool CaptionMap::loadFromFile(const std::string& path)
{
    m_entries.clear();
    if (!std::filesystem::exists(path))
    {
        // Absent captions.json is a valid state — not every project
        // ships captions. Silent return.
        return false;
    }

    const auto parsed = JsonSizeCap::loadJsonWithSizeCap(
        path, "CaptionMap");
    if (!parsed.has_value())
    {
        Logger::warning("[CaptionMap] Failed to parse '" + path + "'.");
        return false;
    }

    if (!populateFromJson(*parsed, m_entries))
    {
        return false;
    }
    Logger::info("[CaptionMap] Loaded " +
                 std::to_string(m_entries.size()) +
                 " caption(s) from '" + path + "'.");
    return true;
}

bool CaptionMap::loadFromString(const std::string& jsonText)
{
    m_entries.clear();
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(jsonText);
    }
    catch (const nlohmann::json::parse_error& e)
    {
        Logger::warning(std::string("[CaptionMap] Parse error: ") +
                        e.what());
        return false;
    }
    return populateFromJson(j, m_entries);
}

const Subtitle* CaptionMap::lookup(const std::string& clipPath) const
{
    auto it = m_entries.find(clipPath);
    if (it == m_entries.end())
    {
        return nullptr;
    }
    return &it->second;
}

bool CaptionMap::enqueueFor(const std::string& clipPath,
                            SubtitleQueue& queue) const
{
    const Subtitle* sub = lookup(clipPath);
    if (sub == nullptr)
    {
        return false;
    }
    queue.enqueue(*sub);
    return true;
}

} // namespace Vestige
