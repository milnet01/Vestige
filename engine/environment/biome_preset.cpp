/// @file biome_preset.cpp
/// @brief BiomePreset and BiomeLibrary implementation.
#include "environment/biome_preset.h"

namespace Vestige
{

// --- BiomePreset ---

nlohmann::json BiomePreset::serialize() const
{
    nlohmann::json j;
    j["name"] = name;
    j["groundMaterialPath"] = groundMaterialPath;

    nlohmann::json foliageArr = nlohmann::json::array();
    for (const auto& layer : foliageLayers)
    {
        foliageArr.push_back({{"typeId", layer.typeId}, {"density", layer.density}});
    }
    j["foliageLayers"] = foliageArr;

    nlohmann::json scatterArr = nlohmann::json::array();
    for (const auto& layer : scatterLayers)
    {
        scatterArr.push_back({{"typeId", layer.typeId}, {"density", layer.density}});
    }
    j["scatterLayers"] = scatterArr;

    nlohmann::json treeArr = nlohmann::json::array();
    for (const auto& layer : treeLayers)
    {
        treeArr.push_back({{"speciesId", layer.speciesId}, {"density", layer.density}});
    }
    j["treeLayers"] = treeArr;

    return j;
}

BiomePreset BiomePreset::deserialize(const nlohmann::json& j)
{
    BiomePreset preset;
    preset.name = j.value("name", "Unknown");
    preset.groundMaterialPath = j.value("groundMaterialPath", "");

    if (j.contains("foliageLayers") && j["foliageLayers"].is_array())
    {
        for (const auto& item : j["foliageLayers"])
        {
            FoliageLayer layer;
            layer.typeId = item.value("typeId", 0u);
            layer.density = item.value("density", 2.0f);
            preset.foliageLayers.push_back(layer);
        }
    }

    if (j.contains("scatterLayers") && j["scatterLayers"].is_array())
    {
        for (const auto& item : j["scatterLayers"])
        {
            ScatterLayer layer;
            layer.typeId = item.value("typeId", 0u);
            layer.density = item.value("density", 0.5f);
            preset.scatterLayers.push_back(layer);
        }
    }

    if (j.contains("treeLayers") && j["treeLayers"].is_array())
    {
        for (const auto& item : j["treeLayers"])
        {
            TreeLayer layer;
            layer.speciesId = item.value("speciesId", 0u);
            layer.density = item.value("density", 0.05f);
            preset.treeLayers.push_back(layer);
        }
    }

    return preset;
}

// --- BiomeLibrary ---

BiomeLibrary::BiomeLibrary()
{
    createBuiltInPresets();
}

const BiomePreset& BiomeLibrary::getPreset(int index) const
{
    static BiomePreset empty;
    if (index < 0 || index >= static_cast<int>(m_presets.size()))
    {
        return empty;
    }
    return m_presets[index];
}

std::vector<std::string> BiomeLibrary::getPresetNames() const
{
    std::vector<std::string> names;
    names.reserve(m_presets.size());
    for (const auto& preset : m_presets)
    {
        names.push_back(preset.name);
    }
    return names;
}

void BiomeLibrary::addPreset(const BiomePreset& preset)
{
    m_presets.push_back(preset);
}

nlohmann::json BiomeLibrary::serializeUserPresets() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (int i = m_builtInCount; i < static_cast<int>(m_presets.size()); ++i)
    {
        arr.push_back(m_presets[i].serialize());
    }
    return arr;
}

void BiomeLibrary::deserializeUserPresets(const nlohmann::json& j)
{
    if (!j.is_array()) return;
    for (const auto& item : j)
    {
        m_presets.push_back(BiomePreset::deserialize(item));
    }
}

void BiomeLibrary::createBuiltInPresets()
{
    // Garden — lush courtyard with grass, flowers, small rocks, olive trees
    {
        BiomePreset garden;
        garden.name = "Garden";
        garden.foliageLayers = {{0, 3.0f}, {2, 0.8f}};  // Short grass + flowers
        garden.scatterLayers = {{0, 0.3f}};  // Small rocks
        garden.treeLayers = {{0, 0.02f}};    // Olive trees
        m_presets.push_back(garden);
    }

    // Desert — sparse, mostly rocks and sand-colored scrub
    {
        BiomePreset desert;
        desert.name = "Desert";
        desert.foliageLayers = {{1, 0.5f}};   // Tall sparse grass
        desert.scatterLayers = {{1, 0.8f}, {3, 0.5f}};  // Large rocks + pebbles
        m_presets.push_back(desert);
    }

    // Temple Courtyard — stone paths, manicured grass, cedars
    {
        BiomePreset temple;
        temple.name = "Temple Courtyard";
        temple.foliageLayers = {{0, 4.0f}};   // Dense short grass
        temple.scatterLayers = {{0, 0.1f}};   // Few scattered rocks
        temple.treeLayers = {{1, 0.03f}};     // Cedar trees
        m_presets.push_back(temple);
    }

    // Cedar Forest — dense forest floor
    {
        BiomePreset forest;
        forest.name = "Cedar Forest";
        forest.foliageLayers = {{3, 2.0f}, {0, 1.5f}};  // Ferns + grass
        forest.scatterLayers = {{0, 0.4f}, {2, 0.2f}};  // Rocks + debris
        forest.treeLayers = {{1, 0.08f}, {3, 0.03f}};   // Cedars + oaks
        m_presets.push_back(forest);
    }

    m_builtInCount = static_cast<int>(m_presets.size());
}

} // namespace Vestige
