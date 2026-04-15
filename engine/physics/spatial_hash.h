// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file spatial_hash.h
/// @brief Spatial hash grid for broad-phase collision detection.
///
/// Based on Teschner et al. 2003, "Optimized Spatial Hashing for Collision
/// Detection of Deformable Objects". Uses counting-sort (Müller) for O(N)
/// rebuild with no per-frame allocations after the first build.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief Spatial hash grid for fast neighbor queries on particle systems.
///
/// Designed for cloth self-collision broad phase. Rebuilt each substep
/// using O(N) counting-sort. Query returns all particles within a radius.
class SpatialHash
{
public:
    /// @brief Builds the spatial hash from particle positions.
    /// @param positions Particle position array.
    /// @param count Number of particles.
    /// @param cellSize Grid cell size (typically 2× collision distance).
    void build(const glm::vec3* positions, size_t count, float cellSize);

    /// @brief Finds all particles within radius of a query position.
    /// @param position Query position.
    /// @param radius Search radius.
    /// @param selfIndex Index to exclude from results (the querying particle).
    /// @param result Output: indices of nearby particles (appended, not cleared).
    void query(const glm::vec3& position, float radius, uint32_t selfIndex,
               std::vector<uint32_t>& result) const;

    /// @brief Returns the cell size used in the last build.
    float getCellSize() const;

    /// @brief Returns the number of particles in the hash.
    size_t getEntryCount() const;

private:
    float m_cellSize = 0.1f;
    float m_invCellSize = 10.0f;
    size_t m_tableSize = 0;
    size_t m_count = 0;

    std::vector<uint32_t> m_cellStart;  ///< Prefix-sum offsets per cell (+1 sentinel)
    std::vector<uint32_t> m_entries;    ///< Particle IDs sorted by cell

    const glm::vec3* m_positions = nullptr;  ///< Non-owning pointer to current positions

    /// @brief Hash function from Teschner 2003.
    size_t hashCell(int cx, int cy, int cz) const;

    /// @brief Converts world position to integer cell coordinates.
    glm::ivec3 cellCoord(const glm::vec3& pos) const;
};

} // namespace Vestige
