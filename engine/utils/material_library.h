/// @file material_library.h
/// @brief Save/load reusable material presets as JSON files.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

class Material;
class ResourceManager;

/// @brief Provides save/load of material presets to/from JSON files.
namespace MaterialLibrary
{

/// @brief Saves a material to a JSON file in the materials directory.
/// @param name Preset name (used as filename without extension).
/// @param material The material to serialize.
/// @param resources ResourceManager for resolving texture paths.
/// @param assetPath Base asset directory path.
/// @return True if saved successfully.
bool saveMaterial(const std::string& name, const Material& material,
                  const ResourceManager& resources, const std::string& assetPath);

/// @brief Loads a material preset from a JSON file and applies it to the target material.
/// @param name Preset name (filename without extension).
/// @param target The material to apply loaded properties to.
/// @param resources ResourceManager for loading textures.
/// @param assetPath Base asset directory path.
/// @return True if loaded successfully.
bool loadMaterial(const std::string& name, Material& target,
                  ResourceManager& resources, const std::string& assetPath);

/// @brief Lists all available material preset names in the materials directory.
/// @param assetPath Base asset directory path.
/// @return Vector of preset names (without .json extension).
std::vector<std::string> listPresets(const std::string& assetPath);

} // namespace MaterialLibrary
} // namespace Vestige
