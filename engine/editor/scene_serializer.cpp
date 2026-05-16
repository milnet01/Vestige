// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scene_serializer.cpp
/// @brief Scene save/load implementation.
#include "editor/scene_serializer.h"
#include "utils/atomic_write.h"
#include "utils/entity_serializer.h"
#include "environment/foliage_manager.h"
#include "environment/terrain.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "resource/resource_manager.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <atomic>
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

/// @brief Maximum on-disk size of a .scene file (matches obj_loader /
///        gltf_loader caps). A malicious or corrupt scene file would
///        otherwise be parsed into unbounded memory and OOM-kill the
///        process. (AUDIT H4.)
static constexpr std::uintmax_t MAX_SCENE_FILE_BYTES = 256ull * 1024 * 1024;

/// @brief Opens and parses a .scene file, enforcing the size cap. On
///        success fills ``outJson`` and returns empty string; on failure
///        returns a human-readable error and leaves ``outJson`` empty.
static std::string openAndParseSceneJson(const fs::path& path, json& outJson)
{
    std::error_code ec;
    const std::uintmax_t sz = fs::file_size(path, ec);
    if (ec)
    {
        return "Could not stat file: " + path.string();
    }
    if (sz > MAX_SCENE_FILE_BYTES)
    {
        return "Scene file exceeds " + std::to_string(MAX_SCENE_FILE_BYTES)
             + "-byte cap: " + path.string() + " (" + std::to_string(sz) + " bytes)";
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        return "Could not open file: " + path.string();
    }

    try
    {
        outJson = json::parse(file);
    }
    catch (const json::parse_error& e)
    {
        return "JSON parse error in " + path.string() + ": " + e.what();
    }
    return {};
}

/// @brief Returns the current time as an ISO 8601 UTC string.
static std::string currentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif

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
///
/// Phase 10.9 Slice 5 D7: depth-capped at 128 to prevent a JSON
/// stack-bomb (`{"children":[{"children":[...]}]}` with 10⁵ levels
/// blows the default 8 MB stack). Mirrors the depth cap in
/// `entity_serializer.cpp::deserializeEntityRecursive`. Beyond the
/// cap, returns the partial count instead of recursing — this is
/// only a count, so under-counting is acceptable; the deserialiser
/// will reject the document on the same boundary.
static int countJsonEntities(const json& entities, int depth = 0)
{
    constexpr int kMaxEntityRecursionDepth = 128;
    if (depth > kMaxEntityRecursionDepth)
        return 0;

    int count = 0;
    for (const auto& e : entities)
    {
        count++;
        if (e.contains("children") && e["children"].is_array())
        {
            count += countJsonEntities(e["children"], depth + 1);
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

// ---------------------------------------------------------------------------
// Phase 10.9 Slice 12 Ed11 — scene-envelope atomicity helpers
// ---------------------------------------------------------------------------
//
// The save sequence used to be: write scene.json, re-read it, inject
// environment+terrain, write scene.json again, then write heightmap
// + splatmap as two more atomic operations. Four separate atomic commits
// across four files — a crash between any pair left an inconsistent
// hybrid (new heightmap, old scene.json that still pointed at "no
// terrain", etc.).
//
// Ed11 collapses this to one commit. The terrain side-files (heightmap +
// splatmap) get an epoch token in their filename (e.g.
// `mySceneStem.heightmap.1747349123456-7.r32`) and are written FIRST.
// The scene.json itself acts as the manifest: its `terrain.heightmap_file`
// / `terrain.splatmap_file` keys name the current epoch's files. The
// scene.json atomic write is the single commit point — a crash before
// that write leaves the old scene.json intact (still pointing at the old
// epoch or at no terrain at all), and the orphaned new-epoch side-files
// get swept on the next successful save.
//
// Backwards compatibility: if `heightmap_file` / `splatmap_file` are
// missing from a loaded scene's terrain section, we fall back to the
// pre-Ed11 unsuffixed names (`<stem>.heightmap.r32`,
// `<stem>.splatmap.splat`). Existing scenes on disk keep loading.

/// @brief Generates a monotonic-ish epoch token unique within a save
///        directory. UTC milliseconds + a process-scoped counter; two
///        rapid saves in the same millisecond still get distinct tokens.
static std::string makeEpochToken()
{
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    auto ms = duration_cast<milliseconds>(now).count();
    static std::atomic<unsigned> counter{0};
    return std::to_string(ms) + "-"
         + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

/// @copydoc SceneSerializer::garbageCollectEpochFiles
void SceneSerializer::garbageCollectEpochFiles(const fs::path& dir,
                                                const std::string& stem,
                                                const fs::path& keepHeightmap,
                                                const fs::path& keepSplatmap)
{
    if (dir.empty() || !fs::exists(dir))
    {
        return;
    }

    const std::string heightmapPrefix = stem + ".heightmap.";
    const std::string splatmapPrefix  = stem + ".splatmap.";
    const std::string heightmapSuffix = ".r32";
    const std::string splatmapSuffix  = ".splat";

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        const fs::path& p = entry.path();
        if (p == keepHeightmap || p == keepSplatmap) continue;

        const std::string name = p.filename().string();

        // Legacy unsuffixed names from pre-Ed11 scene files.
        if (name == stem + ".heightmap.r32" || name == stem + ".splatmap.splat")
        {
            std::error_code remEc;
            fs::remove(p, remEc);
            if (remEc)
            {
                Logger::warning("Scene GC: could not remove legacy "
                                + p.string() + ": " + remEc.message());
            }
            continue;
        }

        // Epoch-suffixed names from a previous Ed11 save.
        const bool isHeightmap =
            name.size() > heightmapPrefix.size() + heightmapSuffix.size() &&
            name.compare(0, heightmapPrefix.size(), heightmapPrefix) == 0 &&
            name.compare(name.size() - heightmapSuffix.size(),
                         heightmapSuffix.size(), heightmapSuffix) == 0;
        const bool isSplatmap =
            name.size() > splatmapPrefix.size() + splatmapSuffix.size() &&
            name.compare(0, splatmapPrefix.size(), splatmapPrefix) == 0 &&
            name.compare(name.size() - splatmapSuffix.size(),
                         splatmapSuffix.size(), splatmapSuffix) == 0;

        if (isHeightmap || isSplatmap)
        {
            std::error_code remEc;
            fs::remove(p, remEc);
            if (remEc)
            {
                Logger::warning("Scene GC: could not remove stale "
                                + p.string() + ": " + remEc.message());
            }
        }
    }
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

    // Durable write via the canonical helper (F7: single implementation
    // with fsync(file) + rename + fsync(dir)).
    AtomicWrite::Status status = AtomicWrite::writeFile(path, content);
    if (status != AtomicWrite::Status::Ok)
    {
        result.success = false;
        result.errorMessage = std::string("atomic-write: ")
                            + AtomicWrite::describe(status);
        Logger::error("Scene save failed: " + result.errorMessage
                      + " for " + path.string());
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

    // Read + parse (size-capped) JSON
    json sceneJson;
    if (auto err = openAndParseSceneJson(path, sceneJson); !err.empty())
    {
        result.errorMessage = std::move(err);
        Logger::error("Scene load: " + result.errorMessage);
        return result;
    }

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

    // Rebuild entity ID lookup index after bulk deserialization
    scene.rebuildEntityIndex();

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

    json sceneJson;
    if (!openAndParseSceneJson(path, sceneJson).empty())
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

// --- Environment-aware overloads ---

SceneSerializerResult SceneSerializer::saveScene(
    const Scene& scene,
    const fs::path& path,
    const ResourceManager& resources,
    const FoliageManager* environment,
    const Terrain* terrain)
{
    SceneSerializerResult result;

    // Preserve original creation timestamp if the file already exists.
    std::string existingCreated;
    if (fs::exists(path))
    {
        SceneMetadata existing = readMetadata(path);
        if (!existing.created.empty())
        {
            existingCreated = existing.created;
        }
    }

    // If terrain is present, mint an epoch token and stage the side-files
    // BEFORE building the scene.json that references them. The terrain
    // section embeds the new file names as manifest entries.
    const bool hasTerrain = (terrain && terrain->isInitialized());
    fs::path heightmapPath;
    fs::path splatmapPath;
    std::string heightmapFileName;
    std::string splatmapFileName;
    if (hasTerrain)
    {
        const std::string epoch = makeEpochToken();
        const std::string stem  = path.stem().string();
        heightmapFileName = stem + ".heightmap." + epoch + ".r32";
        splatmapFileName  = stem + ".splatmap."  + epoch + ".splat";
        heightmapPath = path.parent_path() / heightmapFileName;
        splatmapPath  = path.parent_path() / splatmapFileName;
    }

    // Build the full envelope (entities + environment + terrain settings +
    // manifest entries) in one structure so the scene.json write below is
    // the single commit point.
    json sceneJson = buildSceneJson(scene, resources, existingCreated);

    if (environment)
    {
        sceneJson["environment"] = environment->serialize();
    }

    if (hasTerrain)
    {
        json terrainJson = terrain->serializeSettings();
        terrainJson["heightmap_file"] = heightmapFileName;
        terrainJson["splatmap_file"]  = splatmapFileName;
        sceneJson["terrain"] = terrainJson;
    }

    // Stage the terrain side-files first. If either fails, return without
    // touching scene.json — the on-disk scene stays at its previous epoch
    // and the partially-written side-file is an orphan that the next
    // successful save will GC.
    if (hasTerrain)
    {
        if (!terrain->saveHeightmap(heightmapPath))
        {
            result.success = false;
            result.errorMessage = "heightmap write failed for "
                                + heightmapPath.string();
            return result;
        }
        if (!terrain->saveSplatmap(splatmapPath))
        {
            result.success = false;
            result.errorMessage = "splatmap write failed for "
                                + splatmapPath.string();
            return result;
        }
    }

    // The commit. After this returns Ok, the new epoch is durable and
    // referenced; before it returns Ok, the old epoch is still authoritative.
    const std::string content = sceneJson.dump(4);
    AtomicWrite::Status status = AtomicWrite::writeFile(path, content);
    if (status != AtomicWrite::Status::Ok)
    {
        result.success = false;
        result.errorMessage = std::string("atomic-write: ")
                            + AtomicWrite::describe(status);
        Logger::error("Scene save failed: " + result.errorMessage
                      + " for " + path.string());
        return result;
    }

    // Post-commit cleanup. The scene.json is now durable and points at the
    // new epoch; any other epoch-suffixed (or legacy unsuffixed) heightmap
    // / splatmap files in the directory are stale and can go. Failures
    // here are logged but never propagated — a leftover stale file is
    // harmless.
    if (hasTerrain)
    {
        garbageCollectEpochFiles(path.parent_path(), path.stem().string(),
                                 heightmapPath, splatmapPath);
    }

    // Count entities for the result.
    if (sceneJson.contains("entities") && sceneJson["entities"].is_array())
    {
        result.entityCount = countJsonEntities(sceneJson["entities"]);
    }

    if (environment)
    {
        Logger::info("Saved environment data: "
                     + std::to_string(environment->getTotalFoliageCount())
                     + " instances");
    }

    result.success = true;
    Logger::info("Saved scene '" + scene.getName() + "' to " + path.string()
                 + " (" + std::to_string(result.entityCount) + " entities)");

    return result;
}

SceneSerializerResult SceneSerializer::loadScene(
    Scene& scene,
    const fs::path& path,
    ResourceManager& resources,
    FoliageManager* environment,
    Terrain* terrain)
{
    // Standard entity load
    SceneSerializerResult result = loadScene(scene, path, resources);
    if (!result.success)
    {
        return result;
    }

    // Load environment/terrain data from the same file (reuse size-capped helper)
    json sceneJson;
    if (!openAndParseSceneJson(path, sceneJson).empty())
    {
        return result;
    }

    if (environment && sceneJson.contains("environment") && sceneJson["environment"].is_object())
    {
        environment->deserialize(sceneJson["environment"]);
    }

    if (terrain && sceneJson.contains("terrain") && sceneJson["terrain"].is_object())
    {
        const auto& terrainJson = sceneJson["terrain"];
        terrain->deserializeSettings(terrainJson);

        // Ed11: prefer manifest-named side-files (heightmap_file /
        // splatmap_file keys), fall back to the pre-Ed11 unsuffixed
        // names so existing scenes on disk still load.
        const fs::path dir  = path.parent_path();
        const std::string stem = path.stem().string();

        fs::path heightmapPath;
        if (terrainJson.contains("heightmap_file")
            && terrainJson["heightmap_file"].is_string())
        {
            heightmapPath = dir / terrainJson["heightmap_file"].get<std::string>();
        }
        else
        {
            heightmapPath = dir / (stem + ".heightmap.r32");
        }

        fs::path splatmapPath;
        if (terrainJson.contains("splatmap_file")
            && terrainJson["splatmap_file"].is_string())
        {
            splatmapPath = dir / terrainJson["splatmap_file"].get<std::string>();
        }
        else
        {
            splatmapPath = dir / (stem + ".splatmap.splat");
        }

        if (fs::exists(heightmapPath))
        {
            terrain->loadHeightmap(heightmapPath);
        }
        if (fs::exists(splatmapPath))
        {
            terrain->loadSplatmap(splatmapPath);
        }
    }

    return result;
}

} // namespace Vestige
