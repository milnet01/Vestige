// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_atlas.cpp
/// @brief SpriteAtlas — TexturePacker JSON-Array atlas loader.
#include "renderer/sprite_atlas.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Vestige
{

std::shared_ptr<SpriteAtlas> SpriteAtlas::loadFromJson(const std::string& jsonPath)
{
    std::ifstream file(jsonPath);
    if (!file.is_open())
    {
        Logger::error("[SpriteAtlas] Could not open " + jsonPath);
        return nullptr;
    }

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        Logger::error(std::string("[SpriteAtlas] JSON parse error in ") + jsonPath +
                      ": " + e.what());
        return nullptr;
    }

    if (!root.contains("frames") || !root.contains("meta"))
    {
        Logger::error("[SpriteAtlas] Missing 'frames' or 'meta' in " + jsonPath);
        return nullptr;
    }

    auto atlas = std::make_shared<SpriteAtlas>();
    atlas->m_sourcePath = jsonPath;

    // --- meta block ---
    const auto& meta = root["meta"];
    if (meta.contains("size"))
    {
        const auto& sz = meta["size"];
        atlas->m_atlasSize = glm::vec2(sz.value("w", 0), sz.value("h", 0));
    }
    if (meta.contains("image") && meta["image"].is_string())
    {
        atlas->m_imageName = meta["image"].get<std::string>();
    }

    if (atlas->m_atlasSize.x <= 0.0f || atlas->m_atlasSize.y <= 0.0f)
    {
        Logger::error("[SpriteAtlas] Invalid atlas size in meta block (" + jsonPath + ")");
        return nullptr;
    }

    // --- frames block ---
    //
    // TexturePacker supports two JSON shapes: an array (`"frames": [ {...}, ... ]`)
    // and a hash (`"frames": { "name": {...}, ... }`). The array form preserves
    // declaration order and is the default since v4.x; the hash form is legacy
    // but common in Phaser / melonJS pipelines, so we accept both.
    const auto& frames = root["frames"];
    auto parseFrameObject = [&](const nlohmann::json& entry, const std::string& fallbackName)
    {
        SpriteAtlasFrame f;
        f.name = entry.value("filename", fallbackName);

        if (!entry.contains("frame"))
        {
            Logger::error("[SpriteAtlas] Frame '" + f.name + "' has no 'frame' rect");
            return false;
        }

        const auto& r = entry["frame"];
        const float x = r.value("x", 0.0f);
        const float y = r.value("y", 0.0f);
        const float w = r.value("w", 0.0f);
        const float h = r.value("h", 0.0f);
        if (w <= 0.0f || h <= 0.0f)
        {
            Logger::error("[SpriteAtlas] Frame '" + f.name + "' has degenerate size");
            return false;
        }

        f.uv = glm::vec4(x / atlas->m_atlasSize.x,
                         y / atlas->m_atlasSize.y,
                         (x + w) / atlas->m_atlasSize.x,
                         (y + h) / atlas->m_atlasSize.y);

        // Prefer sourceSize if the packer recorded trim info; fall back to
        // the packed rect size for non-trimmed atlases.
        if (entry.contains("sourceSize"))
        {
            const auto& ss = entry["sourceSize"];
            f.sourceSize = glm::vec2(ss.value("w", w), ss.value("h", h));
        }
        else
        {
            f.sourceSize = glm::vec2(w, h);
        }

        // Optional explicit pivot (Aseprite / TexturePacker both support this).
        if (entry.contains("pivot") && entry["pivot"].is_object())
        {
            const auto& p = entry["pivot"];
            f.pivot = glm::vec2(p.value("x", 0.5f), p.value("y", 0.5f));
        }
        else
        {
            f.pivot = glm::vec2(0.5f, 0.5f);
        }

        atlas->m_index[f.name] = atlas->m_frames.size();
        atlas->m_frames.push_back(std::move(f));
        return true;
    };

    if (frames.is_array())
    {
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            const auto& entry = frames[i];
            if (!parseFrameObject(entry, "frame_" + std::to_string(i)))
            {
                return nullptr;
            }
        }
    }
    else if (frames.is_object())
    {
        for (auto it = frames.begin(); it != frames.end(); ++it)
        {
            if (!parseFrameObject(it.value(), it.key()))
            {
                return nullptr;
            }
        }
    }
    else
    {
        Logger::error("[SpriteAtlas] 'frames' is neither array nor object in " + jsonPath);
        return nullptr;
    }

    if (atlas->m_frames.empty())
    {
        Logger::error("[SpriteAtlas] No frames parsed from " + jsonPath);
        return nullptr;
    }

    Logger::info("[SpriteAtlas] Loaded " + std::to_string(atlas->m_frames.size()) +
                 " frames from " + jsonPath);
    return atlas;
}

const SpriteAtlasFrame* SpriteAtlas::find(const std::string& name) const
{
    auto it = m_index.find(name);
    if (it == m_index.end())
    {
        return nullptr;
    }
    return &m_frames[it->second];
}

std::vector<std::string> SpriteAtlas::frameNames() const
{
    std::vector<std::string> names;
    names.reserve(m_frames.size());
    for (const auto& f : m_frames)
    {
        names.push_back(f.name);
    }
    return names;
}

} // namespace Vestige
