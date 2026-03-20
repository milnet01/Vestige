/// @file scene_serializer.cpp
/// @brief Scene save/load implementation.
#include "editor/scene_serializer.h"
#include "utils/entity_serializer.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "resource/resource_manager.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Vestige
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Returns the current time as an ISO 8601 UTC string.
static std::string currentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);

    std::ostringstream ss;
    ss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

/// @brief Counts entities in a subtree (including the root).
static int countEntities(const Entity& entity)
{
    int count = 1;
    for (const auto& child : entity.getChildren())
    {
        count += countEntities(*child);
    }
    return count;
}

/// @brief Counts entities in a JSON children array.
static int countJsonEntities(const json& entities)
{
    int count = 0;
    for (const auto& e : entities)
    {
        count++;
        if (e.contains("children") && e["children"].is_array())
        {
            count += countJsonEntities(e["children"]);
        }
    }
    return count;
}

/// @brief Builds the scene envelope JSON (metadata + entities).
static json buildSceneJson(const Scene& scene, const ResourceManager& resources,
                           const std::string& existingCreated)
{
    json root;

    // Scene metadata
    json metadata;
    metadata["format_version"] = SceneSerializer::CURRENT_FORMAT_VERSION;
    metadata["name"] = scene.getName();
    metadata["engine_version"] = SceneSerializer::ENGINE_VERSION;

    if (!existingCreated.empty())
    {
        metadata["created"] = existingCreated;
    }
    else
    {
        metadata["created"] = currentTimestamp();
    }
    metadata["modified"] = currentTimestamp();

    root["vestige_scene"] = metadata;

    // Serialize all root children (skip the hidden root node itself)
    json entities = json::array();
    const Entity* sceneRoot = const_cast<Scene&>(scene).getRoot();
    if (sceneRoot)
    {
        for (const auto& child : sceneRoot->getChildren())
        {
            entities.push_back(EntitySerializer::serializeEntity(*child, resources));
        }
    }

    root["entities"] = entities;

    return root;
}

/// @brief Writes a JSON string to file atomically (write to .tmp, then rename).
static bool atomicWriteFile(const fs::path& path, const std::string& content,
                            std::string& errorOut)
{
    // Ensure parent directory exists
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        errorOut = "Could not create directory " + path.parent_path().string()
                   + ": " + ec.message();
        return false;
    }

    fs::path tmpPath = path;
    tmpPath += ".tmp";

    // Write to temporary file
    {
        std::ofstream out(tmpPath, std::ios::out | std::ios::trunc);
        if (!out.is_open())
        {
            errorOut = "Could not open temp file " + tmpPath.string();
            return false;
        }

        out << content;
        out.flush();

        if (!out.good())
        {
            errorOut = "Write error to " + tmpPath.string();
            out.close();
            fs::remove(tmpPath, ec);
            return false;
        }

        out.close();
    }

    // Atomic rename (POSIX guarantees atomicity on same filesystem)
    fs::rename(tmpPath, path, ec);
    if (ec)
    {
        errorOut = "Could not rename " + tmpPath.string() + " to "
                   + path.string() + ": " + ec.message();
        fs::remove(tmpPath);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Version migration
// ---------------------------------------------------------------------------

/// @brief Migrates scene JSON from an older format version to the current version.
/// @param sceneJson The parsed scene JSON (modified in place).
/// @param fromVersion The format_version in the file.
/// @return True if migration succeeded (or was not needed).
static bool migrateScene(json& /*sceneJson*/, int fromVersion)
{
    // Currently only v1 exists. Future migrations go here:
    // if (fromVersion < 2) { migrateV1toV2(sceneJson); }
    // if (fromVersion < 3) { migrateV2toV3(sceneJson); }

    if (fromVersion > SceneSerializer::CURRENT_FORMAT_VERSION)
    {
        return false;  // Future version — cannot migrate forward
    }

    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SceneSerializerResult SceneSerializer::saveScene(
    const Scene& scene,
    const fs::path& path,
    const ResourceManager& resources)
{
    SceneSerializerResult result;

    // Try to preserve original creation timestamp if the file already exists
    std::string existingCreated;
    if (fs::exists(path))
    {
        SceneMetadata existing = readMetadata(path);
        if (!existing.created.empty())
        {
            existingCreated = existing.created;
        }
    }

    // Build JSON
    json sceneJson = buildSceneJson(scene, resources, existingCreated);

    // Pretty-print with 4-space indent
    std::string content = sceneJson.dump(4);

    // Atomic write
    std::string writeError;
    if (!atomicWriteFile(path, content, writeError))
    {
        result.success = false;
        result.errorMessage = writeError;
        Logger::error("Scene save failed: " + writeError);
        return result;
    }

    // Count entities for the result
    if (sceneJson.contains("entities") && sceneJson["entities"].is_array())
    {
        result.entityCount = countJsonEntities(sceneJson["entities"]);
    }

    result.success = true;
    Logger::info("Saved scene '" + scene.getName() + "' to " + path.string()
                 + " (" + std::to_string(result.entityCount) + " entities)");

    return result;
}

SceneSerializerResult SceneSerializer::loadScene(
    Scene& scene,
    const fs::path& path,
    ResourceManager& resources)
{
    SceneSerializerResult result;

    // Read file
    std::ifstream file(path);
    if (!file.is_open())
    {
        result.errorMessage = "Could not open file: " + path.string();
        Logger::error("Scene load: " + result.errorMessage);
        return result;
    }

    // Parse JSON
    json sceneJson;
    try
    {
        sceneJson = json::parse(file);
    }
    catch (const json::parse_error& e)
    {
        result.errorMessage = "JSON parse error in " + path.string() + ": " + e.what();
        Logger::error("Scene load: " + result.errorMessage);
        return result;
    }
    file.close();

    // Validate scene envelope
    if (!sceneJson.contains("vestige_scene") || !sceneJson["vestige_scene"].is_object())
    {
        result.errorMessage = "Missing 'vestige_scene' header in " + path.string();
        Logger::error("Scene load: " + result.errorMessage);
        return result;
    }

    const auto& meta = sceneJson["vestige_scene"];
    int formatVersion = meta.value("format_version", 0);

    if (formatVersion == 0)
    {
        result.errorMessage = "Missing or invalid format_version in " + path.string();
        Logger::error("Scene load: " + result.errorMessage);
        return result;
    }

    // Version migration
    if (!migrateScene(sceneJson, formatVersion))
    {
        result.errorMessage = "Scene format version " + std::to_string(formatVersion)
                              + " is newer than supported version "
                              + std::to_string(CURRENT_FORMAT_VERSION);
        Logger::error("Scene load: " + result.errorMessage);
        return result;
    }

    // Validate entities array
    if (!sceneJson.contains("entities") || !sceneJson["entities"].is_array())
    {
        result.errorMessage = "Missing 'entities' array in " + path.string();
        Logger::error("Scene load: " + result.errorMessage);
        return result;
    }

    // Clear existing scene
    std::string sceneName = meta.value("name", path.stem().string());
    scene.clearEntities();
    scene.setName(sceneName);

    // Deserialize entities
    const auto& entities = sceneJson["entities"];
    for (const auto& entityJson : entities)
    {
        Entity* entity = EntitySerializer::deserializeEntity(entityJson, scene, resources);
        if (entity)
        {
            result.entityCount++;
            // Count descendants too
            result.entityCount += countEntities(*entity) - 1;
        }
        else
        {
            result.warningCount++;
            Logger::warning("Scene load: failed to deserialize an entity");
        }
    }

    result.success = true;
    Logger::info("Loaded scene '" + sceneName + "' from " + path.string()
                 + " (" + std::to_string(result.entityCount) + " entities, "
                 + std::to_string(result.warningCount) + " warnings)");

    return result;
}

std::string SceneSerializer::serializeToString(
    const Scene& scene,
    const ResourceManager& resources)
{
    try
    {
        json sceneJson = buildSceneJson(scene, resources, "");
        return sceneJson.dump(4);
    }
    catch (const std::exception& e)
    {
        Logger::error("Scene serialize to string failed: " + std::string(e.what()));
        return "";
    }
}

SceneMetadata SceneSerializer::readMetadata(const fs::path& path)
{
    SceneMetadata metadata;

    std::ifstream file(path);
    if (!file.is_open())
    {
        return metadata;
    }

    json sceneJson;
    try
    {
        sceneJson = json::parse(file);
    }
    catch (const json::parse_error&)
    {
        return metadata;
    }

    if (!sceneJson.contains("vestige_scene") || !sceneJson["vestige_scene"].is_object())
    {
        return metadata;
    }

    const auto& meta = sceneJson["vestige_scene"];
    metadata.formatVersion = meta.value("format_version", 0);
    metadata.name = meta.value("name", std::string(""));
    metadata.description = meta.value("description", std::string(""));
    metadata.author = meta.value("author", std::string(""));
    metadata.created = meta.value("created", std::string(""));
    metadata.modified = meta.value("modified", std::string(""));
    metadata.engineVersion = meta.value("engine_version", std::string(""));

    return metadata;
}

} // namespace Vestige
