// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scene_serializer.h
/// @brief Save and load complete scenes to/from JSON files.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "audio/audio_music.h"  // MusicLayer (by value in MusicLayerEntry)

namespace Vestige
{

class AudioMusicPlayer;
class FoliageManager;
class ResourceManager;
class Scene;
class Terrain;

/// @brief One per-scene music layer authored in the scene file's `music`
///        block (Phase 10.9 W8 part 2/2).
struct MusicLayerEntry
{
    MusicLayer layer = MusicLayer::Ambient;
    std::string clipPath;
};

/// @brief Per-scene streaming-music configuration. Populated on load,
///        consumed on save. `loopAll` maps to each layer's loop policy
///        (`AudioMusicPlayer::setLayerLooping`).
struct MusicSceneSettings
{
    std::vector<MusicLayerEntry> layers;
    bool loopAll = true;
};

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
    /// v2 (W8, 2026-06-04) adds the optional `music` block. v1 files load
    /// with an empty `MusicSceneSettings` (backwards-compatible read).
    static constexpr int CURRENT_FORMAT_VERSION = 2;

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

    /// @brief Saves scene with environment, terrain, and music data (W8).
    /// `music` is nullable — pass nullptr to omit the `music` block (the
    /// environment/terrain overload forwards here with nullptr).
    static SceneSerializerResult saveScene(
        const Scene& scene,
        const std::filesystem::path& path,
        const ResourceManager& resources,
        const FoliageManager* environment,
        const Terrain* terrain,
        const MusicSceneSettings* music);

    /// @brief Loads scene with environment, terrain, and music data (W8).
    /// `music` is nullable — when non-null it is populated from the scene's
    /// `music` block (empty when the block is absent, e.g. a v1 file).
    static SceneSerializerResult loadScene(
        Scene& scene,
        const std::filesystem::path& path,
        ResourceManager& resources,
        FoliageManager* environment,
        Terrain* terrain,
        MusicSceneSettings* music);

    /// @brief Reads scene metadata from a file without loading entities.
    /// @param path Input file path.
    /// @return Metadata struct (formatVersion == 0 on failure).
    static SceneMetadata readMetadata(const std::filesystem::path& path);

    /// @brief Removes stale heightmap/splatmap side-files from a scene
    ///        directory, keeping only the pair named in the current
    ///        scene.json manifest (Phase 10.9 Slice 12 Ed11).
    ///
    /// Patterns swept:
    ///   - Epoch-suffixed names matching `<stem>.heightmap.*.r32` /
    ///     `<stem>.splatmap.*.splat` that aren't `keepHeightmap` /
    ///     `keepSplatmap`.
    ///   - Legacy unsuffixed names `<stem>.heightmap.r32` and
    ///     `<stem>.splatmap.splat` (pre-Ed11 layout).
    ///
    /// Files with a different stem are left untouched — two scenes can
    /// share a directory. Failures during removal are logged but never
    /// throw; a stale file is harmless and the next save's GC will retry.
    static void garbageCollectEpochFiles(
        const std::filesystem::path& dir,
        const std::string& stem,
        const std::filesystem::path& keepHeightmap,
        const std::filesystem::path& keepSplatmap);
};

/// @brief Drives an `AudioMusicPlayer` from loaded `MusicSceneSettings`
///        (W8 part 2/2). Stops any previous scene's music, then loads +
///        plays each layer with the `loopAll` policy. Logs + counts a
///        warning per layer that fails to load; remaining layers still
///        load. Safe with empty settings (just clears). Shared by every
///        `SceneSerializer::loadScene` caller (engine start-up + editor
///        Open / Recent / autosave-recover) so the wiring lives in one
///        place. Returns the number of layers that failed to load.
int applyMusicSceneSettings(AudioMusicPlayer& player,
                            const MusicSceneSettings& music);

} // namespace Vestige
