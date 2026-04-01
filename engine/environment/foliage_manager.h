/// @file foliage_manager.h
/// @brief Manages the chunk grid of environment instances with paint/erase/cull API.
#pragma once

#include "environment/density_map.h"
#include "environment/foliage_chunk.h"
#include "environment/foliage_instance.h"
#include "utils/aabb.h"
#include "utils/frustum.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Identifies an instance within the chunk grid (for undo support).
struct FoliageInstanceRef
{
    uint64_t chunkKey;       ///< Packed (gridX, gridZ) key.
    uint32_t typeId;         ///< Foliage type within the chunk.
    FoliageInstance instance; ///< The actual instance data.
};

/// @brief Manages all environment instances across a 2D chunk grid.
///
/// Provides the high-level API for painting and erasing foliage, scatter,
/// and trees. Handles chunk creation, frustum culling, and serialization.
class FoliageManager
{
public:
    FoliageManager();

    /// @brief Paints foliage instances within a circular stamp area.
    /// @param typeId Foliage type to paint.
    /// @param center World-space center of the brush stamp.
    /// @param radius Brush radius in meters.
    /// @param density Instances per square meter.
    /// @param falloff Edge falloff (0 = sharp edge, 1 = full taper).
    /// @param config Type configuration (scale range, tint variation).
    /// @param densityMap Optional density map for spatial modulation (nullptr = no modulation).
    /// @return List of added instances with their chunk keys (for undo).
    std::vector<FoliageInstanceRef> paintFoliage(
        uint32_t typeId,
        const glm::vec3& center,
        float radius,
        float density,
        float falloff,
        const FoliageTypeConfig& config,
        const DensityMap* densityMap = nullptr);

    /// @brief Erases foliage of the given type within a radius.
    /// @return List of removed instances with their chunk keys (for undo).
    std::vector<FoliageInstanceRef> eraseFoliage(
        uint32_t typeId,
        const glm::vec3& center,
        float radius);

    /// @brief Erases all foliage types within a radius.
    /// @return List of removed instances with their chunk keys (for undo).
    std::vector<FoliageInstanceRef> eraseAllFoliage(
        const glm::vec3& center,
        float radius);

    /// @brief Re-adds instances that were previously removed (for redo).
    void restoreFoliage(const std::vector<FoliageInstanceRef>& instances);

    /// @brief Removes specific instances by reference (for undo of paint).
    void removeFoliage(const std::vector<FoliageInstanceRef>& instances);

    /// @brief Returns pointers to all chunks that intersect the view frustum.
    std::vector<const FoliageChunk*> getVisibleChunks(
        const glm::mat4& viewProjection) const;

    /// @brief Gets a chunk by grid coordinates (nullptr if none exists).
    const FoliageChunk* getChunk(int gridX, int gridZ) const;

    /// @brief Returns pointers to all non-empty chunks (for shadow passes that
    /// need casters outside the camera frustum).
    std::vector<const FoliageChunk*> getAllChunks() const;

    // --- Scatter API ---

    /// @brief Paints scatter instances within a circular area.
    /// @param config Scatter type configuration.
    /// @param center World-space center.
    /// @param radius Brush radius.
    /// @param density Instances per m².
    /// @param falloff Edge falloff.
    /// @param densityMap Optional density map for spatial modulation (nullptr = no modulation).
    /// @return References to added instances (for undo).
    std::vector<std::pair<uint64_t, ScatterInstance>> paintScatter(
        const ScatterTypeConfig& config,
        uint32_t meshIndex,
        const glm::vec3& center,
        float radius,
        float density,
        float falloff,
        const DensityMap* densityMap = nullptr);

    /// @brief Erases scatter instances within a radius.
    /// @return References to removed instances (for undo).
    std::vector<std::pair<uint64_t, ScatterInstance>> eraseScatter(
        const glm::vec3& center, float radius);

    /// @brief Directly adds a scatter instance to a chunk (for undo/redo).
    void addScatterDirect(int gridX, int gridZ, const ScatterInstance& instance);

    /// @brief Removes a scatter instance at a specific position (for undo/redo).
    void removeScatterAt(uint64_t chunkKey, const glm::vec3& position);

    // --- Tree API ---

    /// @brief Places a tree at a position with minimum spacing enforcement.
    /// @return References to the placed tree (for undo), empty if too close to another tree.
    std::vector<std::pair<uint64_t, TreeInstance>> placeTree(
        const TreeInstance& instance, float minSpacing);

    /// @brief Removes trees within a radius.
    /// @return References to removed trees (for undo).
    std::vector<std::pair<uint64_t, TreeInstance>> eraseTrees(
        const glm::vec3& center, float radius);

    /// @brief Directly adds a tree to a chunk (for undo/redo).
    void addTreeDirect(int gridX, int gridZ, const TreeInstance& instance);

    /// @brief Removes a tree at a specific position (for undo/redo).
    void removeTreeAt(uint64_t chunkKey, const glm::vec3& position);

    // --- Path clearing ---

    /// @brief Erases all foliage, scatter, and trees along a spline path.
    /// @param path The spline path to clear along.
    /// @param margin Extra clearance beyond the path's width (meters).
    /// @return Total number of instances removed.
    int clearAlongPath(const SplinePath& path, float margin = 0.5f);

    // --- Stats ---

    /// @brief Gets the total number of foliage instances across all chunks.
    int getTotalFoliageCount() const;

    /// @brief Gets the total number of active chunks.
    int getChunkCount() const;

    /// @brief Serializes all environment data to JSON.
    nlohmann::json serialize() const;

    /// @brief Deserializes environment data from JSON.
    void deserialize(const nlohmann::json& j);

    /// @brief Clears all environment data.
    void clear();

    /// @brief Packs grid coordinates into a single uint64_t key.
    static uint64_t packChunkKey(int gridX, int gridZ);

    /// @brief Unpacks a chunk key into grid coordinates.
    static void unpackChunkKey(uint64_t key, int& gridX, int& gridZ);

private:
    /// @brief Gets or creates a chunk at the given grid coordinates.
    FoliageChunk& getOrCreateChunk(int gridX, int gridZ);

    /// @brief Converts a world position to chunk grid coordinates.
    std::pair<int, int> worldToGrid(const glm::vec3& pos) const;

    /// Maps packed (gridX, gridZ) key -> chunk.
    std::unordered_map<uint64_t, std::unique_ptr<FoliageChunk>> m_chunks;
};

} // namespace Vestige
