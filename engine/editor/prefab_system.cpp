/// @file prefab_system.cpp
/// @brief PrefabSystem implementation — save/load entity trees as JSON files.
#include "editor/prefab_system.h"
#include "utils/entity_serializer.h"
#include "scene/entity.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

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
    for (char& c : filename)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*'
            || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            c = '_';
        }
    }

    fs::path filePath = prefabDir / (filename + ".json");
    std::ofstream file(filePath);
    if (!file.is_open())
    {
        Logger::error("Prefab save: could not open " + filePath.string());
        return false;
    }

    file << prefab.dump(4);
    file.close();

    Logger::info("Saved prefab '" + name + "' to " + filePath.string());
    return true;
}

Entity* PrefabSystem::loadPrefab(const std::string& filePath, Scene& scene,
                                 ResourceManager& resources)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        Logger::error("Prefab load: could not open " + filePath);
        return nullptr;
    }

    json prefab;
    try
    {
        prefab = json::parse(file);
    }
    catch (const json::parse_error& e)
    {
        Logger::error("Prefab load: JSON parse error in " + filePath + " — " + e.what());
        return nullptr;
    }

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
