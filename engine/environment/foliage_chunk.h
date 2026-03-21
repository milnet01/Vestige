/// @file foliage_chunk.h
/// @brief 16m x 16m spatial cell storing foliage, scatter, and tree instances.
#pragma once

#include "environment/foliage_instance.h"
#include "utils/aabb.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief A 16m x 16m spatial cell that stores all environment instances within its bounds.
class FoliageChunk
{
public:
    static constexpr float CHUNK_SIZE = 16.0f;

    /// @brief Creates a chunk at the given grid coordinates.
    /// @param gridX X index in the chunk grid.
    /// @param gridZ Z index in the chunk grid.
    FoliageChunk(int gridX, int gridZ);

    // --- Foliage (grass, flowers) ---

    /// @brief Adds a foliage instance of the given type.
    void addFoliage(uint32_t typeId, const FoliageInstance& instance);

    /// @brief Removes all foliage of the given type within a radius of center.
    /// @return Number of instances removed.
    int removeFoliageInRadius(uint32_t typeId, const glm::vec3& center, float radius);

    /// @brief Gets all foliage instances for a given type.
    const std::vector<FoliageInstance>& getFoliage(uint32_t typeId) const;

    /// @brief Gets all foliage type IDs that have instances in this chunk.
    std::vector<uint32_t> getFoliageTypeIds() const;

    // --- Scatter (rocks, debris) ---

    /// @brief Adds a scatter instance.
    void addScatter(const ScatterInstance& instance);

    /// @brief Removes all scatter objects within a radius.
    /// @return Number of instances removed.
    int removeScatterInRadius(const glm::vec3& center, float radius);

    /// @brief Gets all scatter instances.
    const std::vector<ScatterInstance>& getScatter() const;

    // --- Trees ---

    /// @brief Adds a tree instance.
    void addTree(const TreeInstance& instance);

    /// @brief Removes all trees within a radius.
    /// @return Number of instances removed.
    int removeTreesInRadius(const glm::vec3& center, float radius);

    /// @brief Gets all tree instances.
    const std::vector<TreeInstance>& getTrees() const;

    // --- Spatial ---

    /// @brief Gets the world-space bounding box for this chunk.
    AABB getBounds() const;

    /// @brief True if all instance lists are empty.
    bool isEmpty() const;

    /// @brief Total number of instances across all types.
    int getTotalInstanceCount() const;

    /// @brief Gets the grid X coordinate.
    int getGridX() const { return m_gridX; }

    /// @brief Gets the grid Z coordinate.
    int getGridZ() const { return m_gridZ; }

    // --- Serialization ---

    /// @brief Serializes all chunk data to JSON.
    nlohmann::json serialize() const;

    /// @brief Deserializes chunk data from JSON.
    void deserialize(const nlohmann::json& j);

private:
    int m_gridX;
    int m_gridZ;

    /// Maps foliage type ID -> instance list.
    std::unordered_map<uint32_t, std::vector<FoliageInstance>> m_foliage;

    std::vector<ScatterInstance> m_scatter;
    std::vector<TreeInstance> m_trees;

    /// Empty vector returned by getFoliage() when type not found.
    static const std::vector<FoliageInstance> EMPTY_FOLIAGE;
};

} // namespace Vestige
