// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file prefab_system.cpp
/// @brief PrefabSystem implementation — save/load entity trees as JSON files.
#include "editor/prefab_system.h"
#include "utils/atomic_write.h"
#include "utils/entity_serializer.h"
#include "utils/json_size_cap.h"
#include "scene/entity.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Vestige
{

bool PrefabSystem::savePrefab(const Entity& entity, const std::string& name,
                              const ResourceManager& resources,
                              const std::string& assetsPath)
{
    // Ensure prefabs directory exists
    fs::path prefabDir = fs::path(assetsPath) / "prefabs";
    std::error_code ec;
    fs::create_directories(prefabDir, ec);
    if (ec)
    {
        Logger::error("Prefab save: could not create directory " + prefabDir.string()
            + " — " + ec.message());
        return false;
    }

    // Build prefab JSON
    json prefab;
    prefab["name"] = name;
    prefab["version"] = 1;
    prefab["root"] = EntitySerializer::serializeEntity(entity, resources);

    // Write to file (sanitize name for filename)
    std::string filename = name;
    // Remove null bytes
    filename.erase(std::remove(filename.begin(), filename.end(), '\0'), filename.end());
    for (char& c : filename)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*'
            || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            c = '_';
        }
    }
    // Strip leading/trailing dots and spaces
    while (!filename.empty() && (filename.front() == '.' || filename.front() == ' '))
    {
        filename.erase(filename.begin());
    }
    while (!filename.empty() && (filename.back() == '.' || filename.back() == ' '))
    {
        filename.pop_back();
    }
    // Reject directory traversal
    if (filename.find("..") != std::string::npos || filename.empty())
    {
        Logger::error("Prefab save: invalid name after sanitization");
        return false;
    }

    fs::path filePath = prefabDir / (filename + ".json");
    const std::string payload = prefab.dump(4);
    AtomicWrite::Status s = AtomicWrite::writeFile(filePath, payload);
    if (s != AtomicWrite::Status::Ok)
    {
        Logger::error(std::string("Prefab save: ") + AtomicWrite::describe(s)
            + " for " + filePath.string());
        return false;
    }

    Logger::info("Saved prefab '" + name + "' to " + filePath.string());
    return true;
}

Entity* PrefabSystem::loadPrefab(const std::string& filePath, Scene& scene,
                                 ResourceManager& resources)
{
    auto parsed = JsonSizeCap::loadJsonWithSizeCap(
        filePath, "Prefab load", JsonSizeCap::DEFAULT_MAX_BYTES, /*strict=*/true);
    if (!parsed)
    {
        return nullptr;
    }
    const json& prefab = *parsed;

    if (!prefab.contains("root") || !prefab["root"].is_object())
    {
        Logger::error("Prefab load: missing 'root' object in " + filePath);
        return nullptr;
    }

    std::string prefabName = prefab.value("name", std::string("Prefab"));

    Entity* entity = EntitySerializer::deserializeEntity(prefab["root"], scene, resources);
    if (entity)
    {
        Logger::info("Loaded prefab '" + prefabName + "' from " + filePath);
    }

    return entity;
}

std::vector<std::string> PrefabSystem::listPrefabs(const std::string& assetsPath) const
{
    std::vector<std::string> result;

    fs::path prefabDir = fs::path(assetsPath) / "prefabs";
    if (!fs::exists(prefabDir) || !fs::is_directory(prefabDir))
    {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(prefabDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            result.push_back(entry.path().string());
        }
    }

    // Sort alphabetically for consistent menu order
    std::sort(result.begin(), result.end());

    return result;
}

} // namespace Vestige
