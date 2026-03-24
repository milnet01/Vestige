/// @file scene_serializer.h
/// @brief Save and load complete scenes to/from JSON files.
#pragma once

#include <filesystem>
#include <string>

namespace Vestige
{

class FoliageManager;
class ResourceManager;
class Scene;
class Terrain;

/// @brief Result of a scene save or load operation.
struct SceneSerializerResult
{
    bool success = false;
    std::string errorMessage;
    int entityCount = 0;       ///< Number of entities saved/loaded.
    int warningCount = 0;      ///< Number of non-fatal warnings (e.g., missing assets).
};

/// @brief Scene metadata stored in the JSON file header.
struct SceneMetadata
{
    std::string name;
    std::string description;
    std::string author;
    std::string created;       ///< ISO 8601 timestamp.
    std::string modified;      ///< ISO 8601 timestamp.
    std::string engineVersion;
    int formatVersion = 0;
};

/// @brief Saves and loads complete scene state to/from JSON files.
///
/// Uses the existing EntitySerializer for per-entity serialization and wraps
/// the result in a scene envelope with metadata and format versioning.
class SceneSerializer
{
public:
    /// @brief Current scene file format version.
    static constexpr int CURRENT_FORMAT_VERSION = 1;

    /// @brief Engine version string embedded in saved scenes.
    static constexpr const char* ENGINE_VERSION = "0.5.0";

    /// @brief Saves the entire scene to a JSON file.
    /// @param scene The scene to save.
    /// @param path Output file path (typically .scene extension).
    /// @param resources ResourceManager for resolving mesh/texture cache keys.
    /// @return Result with success flag, error message, and entity count.
    static SceneSerializerResult saveScene(
        const Scene& scene,
        const std::filesystem::path& path,
        const ResourceManager& resources);

    /// @brief Loads a scene from a JSON file, replacing all current scene content.
    /// @param scene Target scene — will be cleared before loading.
    /// @param path Input file path.
    /// @param resources ResourceManager for loading meshes/textures/materials.
    /// @return Result with success flag, error message, and entity count.
    static SceneSerializerResult loadScene(
        Scene& scene,
        const std::filesystem::path& path,
        ResourceManager& resources);

    /// @brief Serializes the scene to a JSON string (for auto-save background writes).
    /// @param scene The scene to serialize.
    /// @param resources ResourceManager for resolving cache keys.
    /// @return JSON string, or empty string on failure.
    static std::string serializeToString(
        const Scene& scene,
        const ResourceManager& resources);

    /// @brief Saves scene with environment and terrain data.
    static SceneSerializerResult saveScene(
        const Scene& scene,
        const std::filesystem::path& path,
        const ResourceManager& resources,
        const FoliageManager* environment,
        const Terrain* terrain = nullptr);

    /// @brief Loads scene with environment and terrain data.
    static SceneSerializerResult loadScene(
        Scene& scene,
        const std::filesystem::path& path,
        ResourceManager& resources,
        FoliageManager* environment,
        Terrain* terrain = nullptr);

    /// @brief Reads scene metadata from a file without loading entities.
    /// @param path Input file path.
    /// @return Metadata struct (formatVersion == 0 on failure).
    static SceneMetadata readMetadata(const std::filesystem::path& path);
};

} // namespace Vestige
