// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file biome_preset.h
/// @brief Biome presets — composable layers of foliage, scatter, and trees.
#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief A layer of foliage within a biome.
struct FoliageLayer
{
    uint32_t typeId = 0;
    float density = 2.0f;  ///< Instances per m².
};

/// @brief A layer of scatter objects within a biome.
struct ScatterLayer
{
    uint32_t typeId = 0;
    float density = 0.5f;
};

/// @brief A layer of trees within a biome.
struct TreeLayer
{
    uint32_t speciesId = 0;
    float density = 0.05f;
};

/// @brief A composable biome preset describing a mix of vegetation layers.
struct BiomePreset
{
    std::string name;
    std::string groundMaterialPath;

    std::vector<FoliageLayer> foliageLayers;
    std::vector<ScatterLayer> scatterLayers;
    std::vector<TreeLayer> treeLayers;

    /// @brief Serializes the biome preset to JSON.
    nlohmann::json serialize() const;

    /// @brief Deserializes a biome preset from JSON.
    static BiomePreset deserialize(const nlohmann::json& j);
};

/// @brief Library of built-in and user-defined biome presets.
class BiomeLibrary
{
public:
    BiomeLibrary();

    /// @brief Gets the number of available presets.
    int getPresetCount() const { return static_cast<int>(m_presets.size()); }

    /// @brief Gets a preset by index.
    const BiomePreset& getPreset(int index) const;

    /// @brief Gets all preset names for UI display.
    std::vector<std::string> getPresetNames() const;

    /// @brief Adds a user-defined preset.
    void addPreset(const BiomePreset& preset);

    /// @brief Serializes user presets (not built-ins) to JSON.
    nlohmann::json serializeUserPresets() const;

    /// @brief Deserializes user presets from JSON (adds to library).
    void deserializeUserPresets(const nlohmann::json& j);

private:
    void createBuiltInPresets();

    std::vector<BiomePreset> m_presets;
    int m_builtInCount = 0;  ///< Number of built-in presets (not serialized).
};

} // namespace Vestige
