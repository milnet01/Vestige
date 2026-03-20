/// @file prefab_system.h
/// @brief Save/load entity hierarchies as reusable prefab JSON files.
#pragma once

#include <string>
#include <vector>

namespace Vestige
{

class Entity;
class ResourceManager;
class Scene;

/// @brief Manages saving and loading entity prefabs as JSON files in assets/prefabs/.
class PrefabSystem
{
public:
    /// @brief Saves an entity tree as a prefab JSON file.
    /// @param entity The root entity to save.
    /// @param name Human-readable prefab name (used as filename).
    /// @param resources ResourceManager for resolving resource references.
    /// @param assetsPath Path to the assets/ directory.
    /// @return True if save succeeded.
    bool savePrefab(const Entity& entity, const std::string& name,
                    const ResourceManager& resources, const std::string& assetsPath);

    /// @brief Loads a prefab from a JSON file and instantiates it into the scene.
    /// @param filePath Full path to the prefab .json file.
    /// @param scene Target scene to create entities in.
    /// @param resources ResourceManager for loading meshes/textures/materials.
    /// @return Pointer to the root entity created, or nullptr on failure.
    Entity* loadPrefab(const std::string& filePath, Scene& scene,
                       ResourceManager& resources);

    /// @brief Lists all prefab .json files in the prefabs directory.
    /// @param assetsPath Path to the assets/ directory.
    /// @return Vector of full file paths to prefab JSON files.
    std::vector<std::string> listPrefabs(const std::string& assetsPath) const;
};

} // namespace Vestige
